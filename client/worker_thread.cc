// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "worker_thread.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/time/clock.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "descriptor_poller.h"
#include "glog/logging.h"
#include "ioutil.h"
#include "socket_descriptor.h"
#include "worker_thread_manager.h"

#ifdef _WIN32
# include "socket_helper_win.h"
#endif

namespace devtools_goma {

WorkerThread::ClosureData::ClosureData(
    const char* const location,
    Closure* closure,
    int queuelen,
    int tick,
    Timestamp timestamp)
    : location_(location),
      closure_(closure),
      queuelen_(queuelen),
      tick_(tick),
      timestamp_(timestamp) {
}

void WorkerThread::DelayedClosureImpl::Run() {
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

class WorkerThread::PeriodicClosure {
 public:
  PeriodicClosure(PeriodicClosureId id, const char* const location,
                  Timestamp time_now, absl::Duration period,
                  std::unique_ptr<PermanentClosure> closure)
      : id_(id),
        location_(location),
        last_time_(time_now),
        period_(period),
        closure_(std::move(closure)) {
  }

  PeriodicClosureId id() const { return id_; }
  const char* location() const { return location_; }

  PermanentClosure* GetClosure(Timestamp time_now) {
    CHECK_GE(time_now, last_time_);
    if (time_now >= last_time_ + period_) {
      last_time_ = time_now;
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
  Timestamp last_time_;
  const absl::Duration period_;
  std::unique_ptr<PermanentClosure> closure_;
  DISALLOW_COPY_AND_ASSIGN(PeriodicClosure);
};

WorkerThread::WorkerThread(int pool, std::string name)
    : pool_(pool),
      handle_(kNullThreadHandle),
      tick_(0),
      shutting_down_(false),
      quit_(false),
      name_(std::move(name)),
      auto_lock_stat_next_closure_(nullptr),
      auto_lock_stat_poll_events_(nullptr) {
  VLOG(2) << "WorkerThread " << name_;
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
    max_wait_time_[priority] = absl::ZeroDuration();
  }
}

WorkerThread::~WorkerThread() {
  VLOG(2) << "~WorkerThread " << name_;
  CHECK_EQ(kNullThreadHandle, handle_);
  CHECK(!id_);
}

/* static */
void WorkerThread::Initialize() {
  absl::call_once(key_worker_once_,
                  &WorkerThread::InitializeWorkerKey);
}

/* static */
WorkerThread* WorkerThread::GetCurrentWorker() {
#ifndef _WIN32
  return static_cast<WorkerThread*>(pthread_getspecific(key_worker_));
#else
  return static_cast<WorkerThread*>(TlsGetValue(key_worker_));
#endif
}

WorkerThread::Timestamp WorkerThread::NowCached() {
  if (!now_cached_)
    now_cached_ = timer_.GetDuration();
  return *now_cached_;
}

void WorkerThread::Shutdown() {
  VLOG(2) << "Shutdown " << name_;
  AUTOLOCK(lock, &mu_);
  shutting_down_ = true;
}

void WorkerThread::Quit() {
  VLOG(2) << "Quit " << name_;
  AUTOLOCK(lock, &mu_);
  shutting_down_ = true;
  quit_ = true;
  poller_->Signal();
}

void WorkerThread::ThreadMain() {
#ifndef _WIN32
  pthread_setspecific(key_worker_, this);
#else
  TlsSetValue(key_worker_, this);
#endif
  {
    AUTOLOCK(lock, &mu_);
    id_ = GetCurrentThreadId();
    VLOG(1) << "Start thread:" << id_ << " " << name_;
    cond_id_.Signal();
  }
  while (Dispatch()) { }
  LOG(INFO) << id_ << " Dispatch loop finished " << name_;
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

bool WorkerThread::Dispatch() {
  VLOG(2) << "Dispatch " << name_;
  now_cached_.reset();
  if (!NextClosure()) {
    VLOG(2) << "Dispatch end " << name_;
    return false;
  }
  if (!current_closure_data_)
    return true;
  VLOG(2) << "Loop closure=" << current_closure_data_->closure_ << " " << name_;
  const Timestamp start = timer_.GetDuration();
  current_closure_data_->closure_->Run();
  absl::Duration duration = timer_.GetDuration() - start;
  if (duration > absl::Minutes(1)) {
    LOG(WARNING) << id_ << " closure run too long: " << duration
                 << " " << current_closure_data_->location_
                 << " " << current_closure_data_->closure_;
  }
  return true;
}

absl::once_flag WorkerThread::key_worker_once_;

#ifndef _WIN32
pthread_key_t WorkerThread::key_worker_;
#else
DWORD WorkerThread::key_worker_ = TLS_OUT_OF_INDEXES;
#endif

SocketDescriptor* WorkerThread::RegisterSocketDescriptor(ScopedSocket&& fd,
                                                         Priority priority) {
  VLOG(2) << "RegisterSocketDescriptor " << name_;
  AUTOLOCK(lock, &mu_);
  DCHECK_LT(priority, PRIORITY_IMMEDIATE);
  auto d = absl::make_unique<SocketDescriptor>(std::move(fd), priority, this);
  auto* d_ptr = d.get();
  CHECK(descriptors_.emplace(d_ptr->fd(), std::move(d)).second);
  return d_ptr;
}

ScopedSocket WorkerThread::DeleteSocketDescriptor(
    SocketDescriptor* d) {
  VLOG(2) << "DeleteSocketDescriptor " << name_;
  AUTOLOCK(lock, &mu_);
  poller_->UnregisterDescriptor(d);
  ScopedSocket fd(d->ReleaseFd());
  if (fd.valid()) {
    descriptors_.erase(fd.get());
  }
  return fd;
}

void WorkerThread::RegisterPeriodicClosure(
    PeriodicClosureId id, const char* const location,
    absl::Duration period, std::unique_ptr<PermanentClosure> closure) {
  VLOG(2) << "RegisterPeriodicClosure " << name_;
  AUTOLOCK(lock, &mu_);
  periodic_closures_.emplace_back(
     new PeriodicClosure(id, location, NowCached(), period,
                         std::move(closure)));
}

void WorkerThread::UnregisterPeriodicClosure(
    PeriodicClosureId id, UnregisteredClosureData* data) {
  VLOG(2) << "UnregisterPeriodicClosure " << name_;
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

void WorkerThread::RunClosure(const char* const location, Closure* closure,
                              Priority priority) {
  VLOG(2) << "RunClosure " << name_;
  DCHECK_GE(priority, PRIORITY_MIN);
  DCHECK_LT(priority, NUM_PRIORITIES);
  {
    AUTOLOCK(lock, &mu_);
    AddClosure(location, priority, closure);
    // If this is the same thread, or this worker is running some closure
    // (or in other words, this worker is not in select wait),
    // next Dispatch could pick a closure from pendings_, so we don't need
    // to signal via pipe.
    if (THREAD_ID_IS_SELF(id_) || current_closure_data_)
      return;
  }
  // send select loop something to read about, so new pendings will be
  // processed soon.
  poller_->Signal();
}

WorkerThread::CancelableClosure* WorkerThread::RunDelayedClosure(
    const char* const location,
    absl::Duration delay, Closure* closure) {
  VLOG(2) << "RunDelayedClosure " << name_;
  AUTOLOCK(lock, &mu_);
  DelayedClosureImpl* delayed_closure =
      new DelayedClosureImpl(location, NowCached() + delay, closure);
  delayed_pendings_.push(delayed_closure);
  return delayed_closure;
}

size_t WorkerThread::load() const {
  AUTOLOCK(lock, &mu_);
  size_t n = 0;
  if (current_closure_data_) {
    n += 1;
  }
  n += descriptors_.size();
  for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
    int w = 1 << priority;
    n += pendings_[priority].size() * w;
  }
  return n;
}

size_t WorkerThread::pendings() const {
  AUTOLOCK(lock, &mu_);
  size_t n = 0;
  for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
    n += pendings_[priority].size();
  }
  return n;
}

bool WorkerThread::IsIdle() const {
  AUTOLOCK(lock, &mu_);
  return !current_closure_data_ && descriptors_.size() == 0;
}

string WorkerThread::DebugString() const {
  AUTOLOCK(lock, &mu_);
  std::ostringstream s;
  s << "thread[" << id_ << "/" << name_ << "] ";
  s << " tick=" << tick_;
  if (current_closure_data_) {
    s << " " << current_closure_data_->location_;
    s << " " << current_closure_data_->closure_;
  }
  s << ": " << descriptors_.size() << " descriptors";
  s << ": poll_interval=" << poll_interval_;
  s << ": ";
  for (int priority = PRIORITY_MIN; priority < NUM_PRIORITIES; ++priority) {
    s << Priority_Name(static_cast<Priority>(priority))
      << "[" << pendings_[priority].size() << " pendings "
      << " q=" << max_queuelen_[priority]
      << " w=" << max_wait_time_[priority]
      << "] ";
  }
  s << ": delayed=" << delayed_pendings_.size();
  s << ": periodic=" << periodic_closures_.size();
  if (pool_ != 0)
    s << ": pool=" << pool_;
  return s.str();
}

/* static */
string WorkerThread::Priority_Name(Priority priority) {
  switch (priority) {
    case PRIORITY_LOW: return "PriLow";
    case PRIORITY_MED: return "PriMed";
    case PRIORITY_HIGH: return "PriHigh";
    case PRIORITY_IMMEDIATE: return "PriImmediate";
    default:
      break;
  }
  std::ostringstream ss;
  ss << "PriUnknown[" << priority << "]";
  return ss.str();
}

bool WorkerThread::NextClosure() {
  AUTOLOCK_WITH_STAT(lock, &mu_, auto_lock_stat_next_closure_);
  VLOG(5) << "NextClosure " << name_;
  DCHECK(!now_cached_);  // NowCached() will get new time
  ++tick_;
  current_closure_data_.reset();

  // Default descriptor polling timeout.
  // If there are pending closures, it will check descriptors without timeout.
  // If there are deplayed closures, it will reduce intervals to the nearest
  // delayed closure.
  constexpr absl::Duration kPollInterval = absl::Milliseconds(500);

  poll_interval_ = kPollInterval;

  int priority = PRIORITY_IMMEDIATE;
  for (priority = PRIORITY_IMMEDIATE; priority >= PRIORITY_MIN; --priority) {
    if (!pendings_[priority].empty()) {
      // PRIORITY_IMMEDIATE has higher priority than descriptors.
      if (priority == PRIORITY_IMMEDIATE) {
        current_closure_data_ = GetClosure(static_cast<Priority>(priority));
        return true;
      }
      // For lower priorities, descriptor availability is checked before
      // running the closures.
      poll_interval_ = absl::ZeroDuration();
      break;
    }
  }

  if (poll_interval_ > absl::ZeroDuration() && !delayed_pendings_.empty()) {
    // Adjust poll_interval for delayed closure.
    absl::Duration next_delay = delayed_pendings_.top()->time() - NowCached();
    if (next_delay < absl::ZeroDuration())
      next_delay = absl::ZeroDuration();
    poll_interval_ = std::min(poll_interval_, next_delay);
  }
  DescriptorPoller::CallbackQueue io_pendings;
  VLOG(2) << "poll_interval=" << poll_interval_;
  CHECK_GE(poll_interval_, absl::ZeroDuration());

  const Timestamp poll_start_time = timer_.GetDuration();
  poller_->PollEvents(descriptors_, poll_interval_, priority, &io_pendings,
                      &mu_, &auto_lock_stat_poll_events_);
  // Updated cached time value.
  now_cached_ = timer_.GetDuration();
  // on Windows, poll time would be 0.51481 or so when no event happened.
  // multiply 1.1 (i.e. 0.55) would be good.
  if (NowCached() - poll_start_time > 1.1 * kPollInterval) {
    LOG(WARNING) << id_ << " poll too slow:"
                 << (NowCached() - poll_start_time) << " nsec"
                 << " interval=" << poll_interval_ << " msec"
                 << " #descriptors=" << descriptors_.size()
                 << " priority=" << priority;
    if (NowCached() - poll_start_time > absl::Seconds(1)) {
      for (const auto& desc : descriptors_) {
        LOG(WARNING) << id_ << " list of sockets on slow poll:"
                     << " fd=" << desc.first << " sd=" << desc.second.get()
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
         (delayed_pendings_.top()->time() < NowCached() || shutting_down_)) {
    DelayedClosureImpl* delayed_closure = delayed_pendings_.top();
    delayed_pendings_.pop();
    AddClosure(delayed_closure->location(), PRIORITY_IMMEDIATE,
               NewCallback(delayed_closure, &DelayedClosureImpl::Run));
  }

  // Check periodic closures.
  for (const auto& periodic_closure : periodic_closures_) {
    PermanentClosure* closure = periodic_closure->GetClosure(NowCached());
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
      auto priority_typed = static_cast<Priority>(priority);
      VLOG(2) << "pendings " << Priority_Name(priority_typed);
      current_closure_data_ = GetClosure(priority_typed);

      if (quit_) {
        // If worker thread is quiting, wake up thread soon.
        poller_->Signal();
      }
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

void WorkerThread::AddClosure(const char* const location, Priority priority,
                              Closure* closure) {
  VLOG(2) << "AddClosure " << name_;
  // mu_ held.
  ClosureData closure_data(location, closure, pendings_[priority].size(), tick_,
                           timer_.GetDuration());
  if (closure_data.queuelen_ > max_queuelen_[priority]) {
    max_queuelen_[priority] = closure_data.queuelen_;
  }
  pendings_[priority].push_back(closure_data);
}

WorkerThread::ClosureData WorkerThread::GetClosure(Priority priority) {
  // mu_ held.
  CHECK(!pendings_[priority].empty());
  ClosureData closure_data = pendings_[priority].front();
  pendings_[priority].pop_front();
  absl::Duration wait_time = timer_.GetDuration() - closure_data.timestamp_;
  if (wait_time > max_wait_time_[priority]) {
    max_wait_time_[priority] = wait_time;
  }
  if (wait_time > absl::Minutes(1)) {
    LOG(WARNING) << id_ << " too long in pending queue "
                 << Priority_Name(priority)
                 << " " << wait_time
                 << " queuelen=" << closure_data.queuelen_
                 << " tick=" << (tick_ - closure_data.tick_);
  }
  return closure_data;
}

void WorkerThread::InitializeWorkerKey() {
#ifndef _WIN32
  pthread_key_create(&key_worker_, nullptr);
#else
  key_worker_ = TlsAlloc();
#endif
}

void WorkerThread::RegisterPollEvent(SocketDescriptor* d,
                                     DescriptorEventType type) {
  VLOG(2) << "RegisterPollEvent " << name_;
  AUTOLOCK(lock, &mu_);
  poller_->RegisterPollEvent(d, type);
}

void WorkerThread::UnregisterPollEvent(SocketDescriptor* d,
                                       DescriptorEventType type) {
  VLOG(2) << "UnregisterPollEvent " << name_;
  AUTOLOCK(lock, &mu_);
  poller_->UnregisterPollEvent(d, type);
}

void WorkerThread::RegisterTimeoutEvent(SocketDescriptor* d) {
  VLOG(2) << "RegisterTimeoutEvent " << name_;
  AUTOLOCK(lock, &mu_);
  poller_->RegisterTimeoutEvent(d);
}

void WorkerThread::UnregisterTimeoutEvent(SocketDescriptor* d) {
  VLOG(2) << "UnregisterTimeoutEvent " << name_;
  AUTOLOCK(lock, &mu_);
  poller_->UnregisterTimeoutEvent(d);
}

void WorkerThread::Start() {
  VLOG(2) << "Start " << name_;
  CHECK(PlatformThread::Create(this, &handle_));
  AUTOLOCK(lock, &mu_);
  CHECK_NE(handle_, kNullThreadHandle);
  while (id_ == 0)
    cond_id_.Wait(&mu_);
}

void WorkerThread::Join() {
  VLOG(2) << "Join " << name_;
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
