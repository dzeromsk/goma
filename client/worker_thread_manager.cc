// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "worker_thread_manager.h"

#ifndef _WIN32
#include <limits.h>
#endif  // _WIN32

#include <queue>
#include <sstream>

#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "socket_descriptor.h"
#include "descriptor_poller.h"
#include "glog/logging.h"
#include "simple_timer.h"
#include "worker_thread.h"

#ifdef _WIN32
# include "socket_helper_win.h"
#endif

namespace devtools_goma {

/* static */
const int WorkerThreadManager::kDeadPool = -2;
const int WorkerThreadManager::kAlarmPool = -1;
const int WorkerThreadManager::kFreePool = 0;

// Once we register atfork handler, we can't unregister it.
// However, we'd like to fork at SetUp in each unit test of
// subprocess_task_unittest.
// g_initialize_atfork is used to call pthread_atfork once.
// g_enable_fork will be true when WorkerThreadManager is not alive.
#ifndef _WIN32
static bool g_initialize_atfork = false;
#endif
static bool g_enable_fork = false;

WorkerThreadManager::CancelableClosure::CancelableClosure(
    const char* const location, Closure* closure)
    : closure_(closure),
      location_(location) {
}

WorkerThreadManager::CancelableClosure::~CancelableClosure() {
  CHECK(closure_ == nullptr);
}

void WorkerThreadManager::CancelableClosure::Cancel() {
  delete closure_;
  closure_ = nullptr;
}

const char* WorkerThreadManager::CancelableClosure::location() const {
  return location_;
}

#ifndef _WIN32
static void DontCallForkInWorkerThreadManager() {
  if (!g_enable_fork)
    DLOG(FATAL) << "fork called";
}
#endif

WorkerThreadManager::WorkerThreadManager()
    : alarm_worker_(nullptr),
      next_worker_index_(0),
      next_pool_(kFreePool + 1),
      next_periodic_closure_id_(1) {
  WorkerThread::Initialize();
#ifndef _WIN32
  g_enable_fork = false;
  if (!g_initialize_atfork) {
    pthread_atfork(&DontCallForkInWorkerThreadManager, nullptr, nullptr);
    g_initialize_atfork = true;
  }
#endif
}

WorkerThreadManager::~WorkerThreadManager() {
  CHECK(alarm_worker_ == nullptr);
  for (const auto* worker : workers_) {
    CHECK(worker == nullptr);
  }
  g_enable_fork = true;
}

void WorkerThreadManager::Start(int num_threads) {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  CHECK(workers_.empty());
  CHECK(GetCurrentWorker() == nullptr);
  alarm_worker_ = new WorkerThread(this, kAlarmPool, "alarm_worker");
  alarm_worker_->Start();
  next_worker_index_ = 0;
  for (int i = 0; i < num_threads; ++i) {
    WorkerThread* worker = new WorkerThread(this, kFreePool, "worker");
    worker->Start();
    workers_.push_back(worker);
  }
}

int WorkerThreadManager::StartPool(int num_threads, const std::string& name) {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  CHECK(GetCurrentWorker() == nullptr);
  int pool = next_pool_++;
  for (int i = 0; i < num_threads; ++i) {
    WorkerThread* worker = new WorkerThread(this, pool, name);
    worker->Start();
    workers_.push_back(worker);
  }
  return pool;
}

void WorkerThreadManager::NewThread(OneshotClosure* callback,
                                    const std::string& name) {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  int pool = next_pool_++;
  WorkerThread* worker = new WorkerThread(this, pool, name);
  worker->Start();
  workers_.push_back(worker);
  worker->RunClosure(FROM_HERE, callback, PRIORITY_IMMEDIATE);
}

size_t WorkerThreadManager::num_threads() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return workers_.size();
}

void WorkerThreadManager::Shutdown() {
  LOG(INFO) << "Shutdown";
  AUTO_SHARED_LOCK(lock, &mu_);
  CHECK(GetCurrentWorker() == nullptr);
  if (alarm_worker_ != nullptr)
    alarm_worker_->Shutdown();
  for (auto* worker : workers_) {
    if (worker)
      worker->Shutdown();
  }
}

void WorkerThreadManager::Finish() {
  LOG(INFO) << "Finish";
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  CHECK(GetCurrentWorker() == nullptr);
  if (alarm_worker_ != nullptr)
    alarm_worker_->Quit();
  for (auto* worker : workers_) {
    if (worker)
      worker->Quit();
  }
  // join threads
  if (alarm_worker_) {
    alarm_worker_->Join();
    delete alarm_worker_;
    alarm_worker_ = nullptr;
  }
  for (std::vector<WorkerThread*>::iterator iter = workers_.begin();
       iter != workers_.end();
       ++iter) {
    WorkerThread* worker = *iter;
    if (worker) {
      worker->Join();
      delete worker;
      *iter = nullptr;
    }
  }
}

WorkerThreadManager::ThreadId WorkerThreadManager::GetCurrentThreadId() {
  return devtools_goma::GetCurrentThreadId();
}

bool WorkerThreadManager::Dispatch() {
  WorkerThread* worker = GetCurrentWorker();
  DCHECK(worker) << "thread " << GetCurrentThreadId();
  return worker->Dispatch();
}

SocketDescriptor* WorkerThreadManager::RegisterSocketDescriptor(
    ScopedSocket&& fd, WorkerThreadManager::Priority priority) {
  WorkerThread* worker = GetCurrentWorker();
  DCHECK(worker) << "thread " << GetCurrentThreadId();
  return worker->RegisterSocketDescriptor(std::move(fd), priority);
}

ScopedSocket WorkerThreadManager::DeleteSocketDescriptor(
    SocketDescriptor* d) {
  WorkerThread* worker = GetCurrentWorker();
  DCHECK(worker) << "thead " << GetCurrentThreadId();
  return worker->DeleteSocketDescriptor(d);
}

PeriodicClosureId WorkerThreadManager::NextPeriodicClosureId() {
  AUTOLOCK(lock, &periodic_closure_id_mu_);
  return next_periodic_closure_id_++;
}

PeriodicClosureId WorkerThreadManager::RegisterPeriodicClosure(
    const char* const location,
    int ms, std::unique_ptr<PermanentClosure> closure) {
  DCHECK(alarm_worker_);
  PeriodicClosureId id = NextPeriodicClosureId();

  alarm_worker_->RunClosure(
      FROM_HERE,
      NewCallback(
          &WorkerThreadManager::RegisterPeriodicClosureOnAlarmer,
          alarm_worker_, id, location, ms, std::move(closure)),
      PRIORITY_IMMEDIATE);

  return id;
}

/* static */
void WorkerThreadManager::RegisterPeriodicClosureOnAlarmer(
    WorkerThread* alarmer, PeriodicClosureId id, const char* location,
    int ms, std::unique_ptr<PermanentClosure> closure) {
  alarmer->RegisterPeriodicClosure(id, location, ms, std::move(closure));
}

void WorkerThreadManager::UnregisterPeriodicClosure(PeriodicClosureId id) {
  CHECK(GetCurrentWorker() != alarm_worker_);
  DCHECK(alarm_worker_);

  UnregisteredClosureData unregistered_data;
  alarm_worker_->RunClosure(
      FROM_HERE,
      NewCallback(
          alarm_worker_,
          &WorkerThreadManager::WorkerThread::UnregisterPeriodicClosure,
          id, &unregistered_data),
      PRIORITY_IMMEDIATE);

  SimpleTimer timer;
  timer.Start();
  // Make sure periodic closure was destructed before returning from
  // this method.
  while (!unregistered_data.Done()) {
    const char* location = unregistered_data.Location();
    LOG_EVERY_N(INFO, 100)
        << "UnregisterPeriodicClosure id=" << id
        << " location="
        << (location ? location : "")
        << " timer=" << timer.GetInMilliSeconds() << " [ms]";
    CHECK_LT(timer.GetInMilliSeconds(), 60 * 1000)
        << "UnregisterPeriodicClosure didn't finish in 60 seconds";
    PlatformThread::Sleep(10);
  }
}

void WorkerThreadManager::RunClosure(
    const char* const location,
    Closure* closure, Priority priority) {
  RunClosureInPool(location, kFreePool, closure, priority);
}

void WorkerThreadManager::RunClosureInPool(
    const char* const location,
    int pool, Closure* closure, Priority priority) {
  // Note: having global pendings queue make slower than this implementation?
  WorkerThread* candidate_worker = nullptr;
  {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);  // updates |next_worker_index_|.
    size_t min_load = INT_MAX;
    size_t i;
    for (i = next_worker_index_;
         i < next_worker_index_ + workers_.size();
         ++i) {
      WorkerThread* worker = workers_[i % workers_.size()];
      if (!worker) continue;
      if (worker->pool() != pool) continue;
      if (worker == GetCurrentWorker() && worker->pendings() == 0) {
        candidate_worker = worker;
        break;
      }
      size_t load = worker->load();
      if (load == 0) {
        candidate_worker = worker;
        break;
      }
      if (load < min_load) {
        min_load = load;
        candidate_worker = worker;
      }
    }
    CHECK(candidate_worker);
    next_worker_index_ = (i + 1) % workers_.size();
  }
  return candidate_worker->RunClosure(location, closure, priority);
}

void WorkerThreadManager::RunClosureInThread(
    const char* const location,
    ThreadId id,
    Closure* closure, Priority priority) {
  WorkerThread* worker = GetWorker(id);
  DCHECK(worker);
  worker->RunClosure(location, closure, priority);
}

WorkerThreadManager::CancelableClosure*
WorkerThreadManager::RunDelayedClosureInThread(
    const char* const location,
    ThreadId id, int msec,
    Closure* closure) {
  WorkerThread* worker = GetWorker(id);
  DCHECK(worker);
  return worker->RunDelayedClosure(location, msec, closure);
}

string WorkerThreadManager::DebugString() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  std::ostringstream s;
  s << workers_.size() << " workers\n";
  for (const auto& worker : workers_) {
    if (!worker) continue;
    s << worker->DebugString();
    s << "\n";
  }

  s << "\n";
  return s.str();
}

void WorkerThreadManager::DebugLog() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  int num_idles = 0;
  for (const auto& worker : workers_) {
    if (!worker) continue;
    if (worker->IsIdle()) {
      num_idles++;
      continue;
    }
    LOG(INFO) << worker->DebugString();
  }
  LOG(INFO) << "idle workers:" << num_idles;
}

/* static */
string WorkerThreadManager::Priority_Name(int priority) {
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

WorkerThreadManager::WorkerThread* WorkerThreadManager::GetWorker(ThreadId id) {
  WorkerThread* worker = nullptr;
  {
    AUTO_SHARED_LOCK(lock, &mu_);
    worker = GetWorkerUnlocked(id);
  }
  if (worker != nullptr)
    return worker;
  LOG(FATAL) << "No worker for id=" << id
             << " current=" << GetCurrentThreadId() << " " << DebugString();
  return nullptr;
}

WorkerThreadManager::WorkerThread* WorkerThreadManager::GetWorkerUnlocked(
    ThreadId id) {
  for (auto* worker : workers_) {
    if (worker && id == worker->id()) {
      return worker;
    }
  }
  return nullptr;
}

WorkerThreadManager::WorkerThread* WorkerThreadManager::GetCurrentWorker() {
  return WorkerThread::GetCurrentWorker();
}

WorkerThreadRunner::WorkerThreadRunner(
    WorkerThreadManager* wm,
    const char* const location, OneshotClosure* closure)
    : done_(false) {
  LOG(INFO) << "run closure=" << closure
            << " from " << location;
  wm->RunClosure(location,
                 NewCallback(
                     this,
                     &WorkerThreadRunner::Run,
                     closure),
                 WorkerThreadManager::PRIORITY_MED);
}

WorkerThreadRunner::~WorkerThreadRunner() {
  Wait();
}

void WorkerThreadRunner::Wait() {
  AUTOLOCK(lock, &mu_);
  while (!done_) {
    cond_.Wait(&mu_);
  }
}

bool WorkerThreadRunner::Done() const {
  AUTOLOCK(lock, &mu_);
  return done_;
}

void WorkerThreadRunner::Run(OneshotClosure* closure) {
  closure->Run();
  LOG(INFO) << "done closure=" << closure;
  AUTOLOCK(lock, &mu_);
  done_ = true;
  cond_.Signal();
}

}  // namespace devtools_goma
