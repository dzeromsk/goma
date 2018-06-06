// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "worker_thread.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "absl/memory/memory.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "glog/logging.h"
#include "ioutil.h"
#include "socket_descriptor.h"

#ifdef _WIN32
# include "socket_helper_win.h"
#endif

namespace {

const long long kNanoSecondsPerSecond = 1000000000LL;

}  // anonymous namespace

namespace devtools_goma {

WorkerThreadManager::WorkerThread::ClosureData::ClosureData(
    const char* const location,
    Closure* closure,
    int queuelen,
    int tick,
    long long timestamp_ns)
    : location_(location),
      closure_(closure),
      queuelen_(queuelen),
      tick_(tick),
      timestamp_ns_(timestamp_ns) {
}

WorkerThreadManager::WorkerThread::ClosureData::ClosureData() :
    location_("idle"),
    closure_(nullptr),
    queuelen_(0),
    tick_(0),
    timestamp_ns_(0) {
}

void WorkerThreadManager::WorkerThread::DelayedClosureImpl::Run() {
    Closure* closure = GetClosure();
    if (closure != nullptr) {
      VLOG(3) << "delayed=" << closure;
      closure->Run();
    } else {
      VLOG(1) << "closure " << location() << " has been cancelled";
    }
    // Delete delayed_closure after closure runs.
    delete this;
}

class WorkerThreadManager::WorkerThread::PeriodicClosure {
 public:
  PeriodicClosure(PeriodicClosureId id, const char* const location,
                  double now, int ms, std::unique_ptr<PermanentClosure> closure)
      : id_(id),
        location_(location),
        last_time_(now),
        periodic_ms_(ms),
        closure_(std::move(closure)) {
  }

  PeriodicClosureId id() const { return id_; }
  const char* location() const { return location_; }

  PermanentClosure* GetClosure(double now) {
    CHECK_GE(now, last_time_);
    if (now >= last_time_ + (periodic_ms_ / 1000.0)) {
      last_time_ = now;
      return closure_.get();
    }
    return nullptr;
  }

  PermanentClosure* closure() const { return closure_.get(); }
  std::unique_ptr<PermanentClosure> ReleaseClosure() {
    return std::move(closure_);
  }

 private:
  const PeriodicClosureId id_;
  const char* const location_;
  double last_time_;
  const int periodic_ms_;
  std::unique_ptr<PermanentClosure> closure_;
  DISALLOW_COPY_AND_ASSIGN(PeriodicClosure);
};

WorkerThreadManager::WorkerThread::WorkerThread(WorkerThreadManager* wm,
                                                int pool,
                                                std::string name)
    : wm_(wm),
      pool_(pool),
      handle_(kNullThreadHandle),
      tick_(0),
      now_ns_(0),
      shutting_down_(false),
      quit_(false),
      name_(std::move(name)),
      auto_lock_stat_next_closure_(nullptr),
      auto_lock_stat_poll_events_(nullptr) {
  int pipe_fd[2];
#ifndef _WIN32
  PCHECK(pipe(pipe_fd) == 0);
#else
  CHECK_EQ(async_socketpair(pipe_fd), 0);
#endif
  ScopedSocket pr(pipe_fd[0]);
  PCHECK(pr.SetCloseOnExec());
  PCHECK(pr.SetNonBlocking());
  ScopedSocket pw(pipe_fd[1]);
  PCHECK(pw.SetCloseOnExec());
  PCHECK(pw.SetNonBlocking());
  id_ = 0;
  // poller takes ownership of both pipe fds.
  poller_ = DescriptorPoller::NewDescriptorPoller(
      absl::make_unique<SocketDescriptor>(std::move(pr), PRIORITY_HIGH, this),
      std::move(pw));
  timer_.Start();
  if (g_auto_lock_stats) {
    // TODO: Split stats per pool.
    auto_lock_stat_next_closure_ = g_auto_lock_stats->NewStat(
        "worker_thread::NextClosure");

    auto_lock_stat_poll_events_ = g_auto_lock_stats->NewStat(
        "descriptor_poller::PollEvents");
  }
  for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
    max_queuelen_[priority] = 0;
    max_wait_time_ns_[priority] = 0;
  }
}

WorkerThreadManager::WorkerThread::~WorkerThread() {
  CHECK_EQ(kNullThreadHandle, handle_);
  CHECK(!id_);
}

/* static */
void WorkerThreadManager::WorkerThread::Initialize() {
  absl::call_once(key_worker_once_,
                  &WorkerThreadManager::WorkerThread::InitializeWorkerKey);
}

/* static */
WorkerThreadManager::WorkerThread*
WorkerThreadManager::WorkerThread::GetCurrentWorker() {
#ifndef _WIN32
  return static_cast<WorkerThread*>(pthread_getspecific(key_worker_));
#else
  return static_cast<WorkerThread*>(TlsGetValue(key_worker_));
#endif
}

long long WorkerThreadManager::WorkerThread::NowInNs() {
  if (now_ns_ == 0)
    now_ns_ = timer_.GetInNanoSeconds();
  return now_ns_;
}

double WorkerThreadManager::WorkerThread::Now() {
  return static_cast<double>(NowInNs()) / kNanoSecondsPerSecond;
}

void WorkerThreadManager::WorkerThread::Shutdown() {
  AUTOLOCK(lock, &mu_);
  shutting_down_ = true;
}

void WorkerThreadManager::WorkerThread::Quit() {
  AUTOLOCK(lock, &mu_);
  shutting_down_ = true;
  quit_ = true;
}

void WorkerThreadManager::WorkerThread::ThreadMain() {
#ifndef _WIN32
  pthread_setspecific(key_worker_, this);
#else
  TlsSetValue(key_worker_, this);
#endif
  {
    AUTOLOCK(lock, &mu_);
    while (handle_ == kNullThreadHandle)
      cond_handle_.Wait(&mu_);
  }
  CHECK_NE(handle_, kNullThreadHandle);
  {
    AUTOLOCK(lock, &mu_);
    id_ = GetThreadId(handle_);
    VLOG(1) << "Start thread:" << id_;
    cond_id_.Signal();
  }
  while (Dispatch()) { }
  LOG(INFO) << id_ << " Dispatch loop finished";
  {
    AUTOLOCK(lock, &mu_);
    for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
      CHECK(pendings_[priority].empty());
    }
    CHECK(descriptors_.empty());
    CHECK(periodic_closures_.empty());
    CHECK(quit_);
  }
}

bool WorkerThreadManager::WorkerThread::Dispatch() {
  now_ns_ = 0;
  if (!NextClosure())
    return false;
  if (current_.closure_ == nullptr)
    return true;
  VLOG(2) << "Loop closure=" << current_.closure_;
  long long start_ns = timer_.GetInNanoSeconds();
  current_.closure_->Run();
  long long duration_ns = timer_.GetInNanoSeconds() - start_ns;
  static const double kLongClosureSec = 60.0;
  if (duration_ns > kLongClosureSec * kNanoSecondsPerSecond) {
    LOG(WARNING) << id_ << " closure run too long:"
                 << static_cast<double>(duration_ns) / kNanoSecondsPerSecond
                 << " sec"
                 << " " << current_.location_
                 << " " << current_.closure_;
  }
  return true;
}

absl::once_flag WorkerThreadManager::WorkerThread::key_worker_once_;

#ifndef _WIN32
pthread_key_t WorkerThreadManager::WorkerThread::key_worker_;
#else
DWORD WorkerThreadManager::WorkerThread::key_worker_ = TLS_OUT_OF_INDEXES;
#endif

SocketDescriptor*
WorkerThreadManager::WorkerThread::RegisterSocketDescriptor(
    ScopedSocket&& fd, WorkerThreadManager::Priority priority) {
  AUTOLOCK(lock, &mu_);
  DCHECK_LT(priority, WorkerThreadManager::PRIORITY_IMMEDIATE);
  SocketDescriptor* d = new SocketDescriptor(std::move(fd), priority, this);
  CHECK(descriptors_.insert(std::make_pair(d->fd(), d)).second);
  return d;
}

ScopedSocket WorkerThreadManager::WorkerThread::DeleteSocketDescriptor(
    SocketDescriptor* d) {
  AUTOLOCK(lock, &mu_);
  poller_->UnregisterDescriptor(d);
  ScopedSocket fd(d->ReleaseFd());
  if (fd.valid()) {
    std::map<int, SocketDescriptor*>::iterator found =
        descriptors_.find(fd.get());
    if (found != descriptors_.end()) {
      delete found->second;
      descriptors_.erase(found);
    }
  }
  return fd;
}

void WorkerThreadManager::WorkerThread::RegisterPeriodicClosure(
    PeriodicClosureId id, const char* const location,
    int ms, std::unique_ptr<PermanentClosure> closure) {
  AUTOLOCK(lock, &mu_);
  periodic_closures_.emplace_back(
     new PeriodicClosure(id, location, Now(), ms, std::move(closure)));
}

void WorkerThreadManager::WorkerThread::UnregisterPeriodicClosure(
    PeriodicClosureId id, UnregisteredClosureData* data) {
  DCHECK(data);
  AUTOLOCK(lock, &mu_);
  CHECK_NE(id, kInvalidPeriodicClosureId);

  {
    std::unique_ptr<PermanentClosure> closure;

    auto it = std::find_if(periodic_closures_.begin(), periodic_closures_.end(),
                           [id](const std::unique_ptr<PeriodicClosure>& it) {
                             return it->id() == id;
                           });
    if (it != periodic_closures_.end()) {
      closure = (*it)->ReleaseClosure();
      // Since location is used when this function
      // takes long time, this should be set when it's available.
      data->SetLocation((*it)->location());
      periodic_closures_.erase(it);
    }

    DCHECK(closure) << "Removing unregistered closure id=" << id;

    std::deque<ClosureData> pendings;
    while (!pendings_[PRIORITY_IMMEDIATE].empty()) {
      ClosureData pending_closure =
        pendings_[PRIORITY_IMMEDIATE].front();
      pendings_[PRIORITY_IMMEDIATE].pop_front();
      if (pending_closure.closure_ == closure.get())
        continue;
      pendings.push_back(pending_closure);
    }
    pendings_[PRIORITY_IMMEDIATE].swap(pendings);
  }

  // Notify that |closure| is removed from the queues.
  // SetDone(true) after |closure| has been deleted.
  data->SetDone(true);
}

void WorkerThreadManager::WorkerThread::RunClosure(
    const char* const location,
    Closure* closure, Priority priority) {
  DCHECK_GE(priority, PRIORITY_MIN);
  DCHECK_LT(priority, NUM_PRIORITIES);
  {
    AUTOLOCK(lock, &mu_);
    AddClosure(location, priority, closure);
    // If this is the same thread, or this worker is running some closure
    // (or in other words, this worker is not in select wait),
    // next Dispatch could pick a closure from pendings_, so we don't need
    // to signal via pipe.
    if (THREAD_ID_IS_SELF(id_) || current_.closure_ != nullptr)
      return;
  }
  // send select loop something to read about, so new pendings will be
  // processed soon.
  poller_->Signal();
}

WorkerThreadManager::CancelableClosure*
WorkerThreadManager::WorkerThread::RunDelayedClosure(
    const char* const location,
    int msec, Closure* closure) {
  AUTOLOCK(lock, &mu_);
  DelayedClosureImpl* delayed_closure =
      new DelayedClosureImpl(location, Now() + msec/1000.0, closure);
  delayed_pendings_.push(delayed_closure);
  return delayed_closure;
}

size_t WorkerThreadManager::WorkerThread::load() const {
  AUTOLOCK(lock, &mu_);
  size_t n = 0;
  if (current_.closure_ != nullptr)
    n += 1;
  n += descriptors_.size();
  for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
    int w = 1 << priority;
    n += pendings_[priority].size() * w;
  }
  return n;
}

size_t WorkerThreadManager::WorkerThread::pendings() const {
  AUTOLOCK(lock, &mu_);
  size_t n = 0;
  for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
    n += pendings_[priority].size();
  }
  return n;
}

bool WorkerThreadManager::WorkerThread::IsIdle() const {
  AUTOLOCK(lock, &mu_);
  return current_.closure_ == nullptr && descriptors_.size() == 0;
}

string WorkerThreadManager::WorkerThread::DebugString() const {
  AUTOLOCK(lock, &mu_);
  std::ostringstream s;
  s << "thread[" << id_ << "/" << name_ << "] ";
  s << " tick=" << tick_;
  s << " " << current_.location_;
  if (current_.closure_) {
    s << " " << current_.closure_;
  }
  s << ": " << descriptors_.size() << " descriptors";
  s << ": poll_interval=" << poll_interval_;
  s << ": ";
  for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
    s << WorkerThreadManager::Priority_Name(priority)
      << "[" << pendings_[priority].size() << " pendings "
      << " q=" << max_queuelen_[priority]
      << " w=" << static_cast<double>(max_wait_time_ns_[priority]) /
        kNanoSecondsPerSecond << "] ";
  }
  s << ": delayed=" << delayed_pendings_.size();
  s << ": periodic=" << periodic_closures_.size();
  if (pool_ != 0)
    s << ": pool=" << pool_;
  return s.str();
}

bool WorkerThreadManager::WorkerThread::NextClosure() {
  AUTOLOCK_WITH_STAT(lock, &mu_, auto_lock_stat_next_closure_);
  VLOG(5) << "NextClosure";
  DCHECK_EQ(0, now_ns_);  // Now() and NowInNs() will get new time
  ++tick_;
  current_ = ClosureData();

  // Default descriptor polling timeout.
  // If there are pending closures, it will check descriptors without timeout.
  // If there are deplayed closures, it will reduce intervals to the nearest
  // delayed closure.
  static const int kPollIntervalMilliSec = 500;
  static const long long kPollIntervalNanoSec =
      static_cast<long long>(kPollIntervalMilliSec) * 1000000;

  poll_interval_ = kPollIntervalMilliSec;

  int priority = PRIORITY_IMMEDIATE;
  for (priority = PRIORITY_IMMEDIATE; priority >= PRIORITY_MIN; --priority) {
    if (!pendings_[priority].empty()) {
      // PRIORITY_IMMEDIATE has higher priority than descriptors.
      if (priority == PRIORITY_IMMEDIATE) {
        current_ = GetClosure(
            static_cast<WorkerThreadManager::Priority>(priority));
        return true;
      }
      // For lower priorities, descriptor availability is checked before
      // running the closures.
      poll_interval_ = 0;
      break;
    }
  }

  if (poll_interval_ > 0 && !delayed_pendings_.empty()) {
    // Adjust poll_interval for delayed closure.
    int next_delay = static_cast<int>(
      (delayed_pendings_.top()->time() - Now()) * 1000);
    if (next_delay < 0)
      next_delay = 0;
    poll_interval_ = std::min(poll_interval_, next_delay);
  }
  DescriptorPoller::CallbackQueue io_pendings;
  VLOG(2) << "poll_interval=" << poll_interval_;
  CHECK_GE(poll_interval_, 0);

  long long poll_start_time_ns = timer_.GetInNanoSeconds();
  poller_->PollEvents(descriptors_, poll_interval_,
                      priority, &io_pendings,
                      &mu_, &auto_lock_stat_poll_events_);
  // update NowInNs().
  now_ns_ = timer_.GetInNanoSeconds();
  CHECK_GE(now_ns_, poll_start_time_ns);
  // on Windows, poll time would be 0.51481 or so when no event happened.
  // multiply 1.1 (i.e. 0.55) would be good.
  if (NowInNs() - poll_start_time_ns > 1.1 * kPollIntervalNanoSec) {
    LOG(WARNING) << id_ << " poll too slow:"
                 << (NowInNs() - poll_start_time_ns) << " nsec"
                 << " interval=" << poll_interval_ << " msec"
                 << " #descriptors=" << descriptors_.size()
                 << " priority=" << priority;
    if (NowInNs() - poll_start_time_ns > 1 * kNanoSecondsPerSecond) {
      for (const auto& desc : descriptors_) {
        LOG(WARNING) << id_ << " list of sockets on slow poll:"
                     << " fd=" << desc.first
                     << " sd=" << desc.second
                     << " sd.fd=" << desc.second->fd()
                     << " readable=" << desc.second->IsReadable()
                     << " closed=" << desc.second->IsClosed()
                     << " canreuse=" << desc.second->CanReuse()
                     << " err=" << desc.second->GetLastErrorMessage();
      }
    }
  }

  // Check delayed closures.
  while (!delayed_pendings_.empty() &&
         (delayed_pendings_.top()->time() < Now() || shutting_down_)) {
    DelayedClosureImpl* delayed_closure = delayed_pendings_.top();
    delayed_pendings_.pop();
    AddClosure(delayed_closure->location(), PRIORITY_IMMEDIATE,
               NewCallback(delayed_closure, &DelayedClosureImpl::Run));
  }

  // Check periodic closures.
  for (const auto& periodic_closure : periodic_closures_) {
    PermanentClosure* closure = periodic_closure->GetClosure(Now());
    if (closure != nullptr) {
      VLOG(3) << "periodic=" << closure;
      AddClosure(periodic_closure->location(),
                 PRIORITY_IMMEDIATE, closure);
    }
  }

  // Check descriptors I/O.
  for (auto& iter : io_pendings) {
    Priority io_priority = iter.first;
    std::deque<OneshotClosure*>& pendings = iter.second;
    while (!pendings.empty()) {
      // TODO: use original location
      AddClosure(FROM_HERE, io_priority, pendings.front());
      pendings.pop_front();
    }
  }

  // Check pendings again.
  for (priority = PRIORITY_IMMEDIATE; priority >= PRIORITY_MIN; --priority) {
    if (!pendings_[priority].empty()) {
      VLOG(2) << "pendings " << WorkerThreadManager::Priority_Name(priority);
      current_ = GetClosure(
          static_cast<WorkerThreadManager::Priority>(priority));
      return true;
    }
  }

  // No pendings.
  DCHECK_LT(priority, PRIORITY_MIN);
  if (quit_) {
    VLOG(3) << "NextClosure: terminating";
    if (delayed_pendings_.empty() &&
        periodic_closures_.empty() &&
        descriptors_.empty()) {
      pool_ = WorkerThreadManager::kDeadPool;
      return false;
    }
    LOG(INFO) << "NextClosure: terminating but still active "
              << " delayed_pendings=" << delayed_pendings_.size()
              << " periodic_closures=" << periodic_closures_.size()
              << " descriptors=" << descriptors_.empty();
  }
  VLOG(4) << "NextClosure: no closure to run";
  return true;
}

void WorkerThreadManager::WorkerThread::AddClosure(
    const char* const location,
    WorkerThreadManager::Priority priority,
    Closure* closure) {
  // mu_ held.
  ClosureData closure_data(location, closure,
                           pendings_[priority].size(),
                           tick_,
                           timer_.GetInNanoSeconds());
  if (closure_data.queuelen_ > max_queuelen_[priority]) {
    max_queuelen_[priority] = closure_data.queuelen_;
  }
  pendings_[priority].push_back(closure_data);
}

WorkerThreadManager::WorkerThread::ClosureData
WorkerThreadManager::WorkerThread::GetClosure(
    WorkerThreadManager::Priority priority) {
  // mu_ held.
  CHECK(!pendings_[priority].empty());
  ClosureData closure_data = pendings_[priority].front();
  pendings_[priority].pop_front();
  long long wait_time_ns =
      timer_.GetInNanoSeconds() - closure_data.timestamp_ns_;
  static const long long kLongWaitTimeNanoSec = 60 * kNanoSecondsPerSecond;
  if (wait_time_ns > max_wait_time_ns_[priority]) {
    max_wait_time_ns_[priority] = wait_time_ns;
  }
  if (wait_time_ns > kLongWaitTimeNanoSec) {
    LOG(WARNING) << id_ << " too long in pending queue "
                 << WorkerThreadManager::Priority_Name(priority)
                 << " "
                 << static_cast<double>(wait_time_ns) / kNanoSecondsPerSecond
                 << " [sec] queuelen=" << closure_data.queuelen_
                 << " tick=" << (tick_ - closure_data.tick_);
  }
  return closure_data;
}

void WorkerThreadManager::WorkerThread::InitializeWorkerKey() {
#ifndef _WIN32
  pthread_key_create(&key_worker_, nullptr);
#else
  key_worker_ = TlsAlloc();
#endif
}

void WorkerThreadManager::WorkerThread::RegisterPollEvent(
    SocketDescriptor* d, DescriptorPoller::EventType type) {
  AUTOLOCK(lock, &mu_);
  poller_->RegisterPollEvent(d, type);
}

void WorkerThreadManager::WorkerThread::UnregisterPollEvent(
    SocketDescriptor* d, DescriptorPoller::EventType type) {
  AUTOLOCK(lock, &mu_);
  poller_->UnregisterPollEvent(d, type);
}

void WorkerThreadManager::WorkerThread::RegisterTimeoutEvent(
    SocketDescriptor* d) {
  AUTOLOCK(lock, &mu_);
  poller_->RegisterTimeoutEvent(d);
}

void WorkerThreadManager::WorkerThread::UnregisterTimeoutEvent(
    SocketDescriptor* d) {
  AUTOLOCK(lock, &mu_);
  poller_->UnregisterTimeoutEvent(d);
}

void WorkerThreadManager::WorkerThread::Start() {
  CHECK(PlatformThread::Create(this, &handle_));
  AUTOLOCK(lock, &mu_);
  CHECK_NE(handle_, kNullThreadHandle);
  cond_handle_.Signal();
  while (id_ == 0)
    cond_id_.Wait(&mu_);
}

void WorkerThreadManager::WorkerThread::Join() {
  if (handle_ != kNullThreadHandle) {
    LOG(INFO) << "Join thread:" << DebugString();
    {
      AUTOLOCK(lock, &mu_);
      CHECK(quit_);
    }
    FlushLogFiles();
    PlatformThread::Join(handle_);
  }
  handle_ = kNullThreadHandle;
  id_ = 0;
}

}  // namespace devtools_goma
