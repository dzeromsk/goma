// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_WORKER_THREAD_H_
#define DEVTOOLS_GOMA_CLIENT_WORKER_THREAD_H_

#include <deque>
#include <map>
#include <queue>
#include <vector>

#include "absl/base/call_once.h"
#include "basictypes.h"
#include "callback.h"
#include "descriptor_poller.h"
#include "lockhelper.h"
#include "platform_thread.h"
#include "scoped_fd.h"
#include "simple_timer.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

class AutoLockStat;
class SocketDescriptor;

class WorkerThreadManager::WorkerThread : public PlatformThread::Delegate {
 public:
  class DelayedClosureImpl : public CancelableClosure {
   public:
    DelayedClosureImpl(const char* const location,
                       double t, Closure* closure)
        : CancelableClosure(location, closure), time_(t) {}
    double time() const { return time_; }
    Closure* GetClosure() {
      Closure* closure = closure_;
      closure_ = NULL;
      return closure;
    }

   private:
    friend class WorkerThread;
    friend class WorkerThreadTest;
    ~DelayedClosureImpl() override {}

    // Run closure if it is still set, and destroy itself.
    void Run();

    double time_;
    DISALLOW_COPY_AND_ASSIGN(DelayedClosureImpl);
  };

  static void Initialize();
  static WorkerThread* GetCurrentWorker();

  WorkerThread(WorkerThreadManager* wm, int pool, std::string name);
  ~WorkerThread() override;

  int pool() const { return pool_; }
  ThreadId id() const { return id_; }
  long long NowInNs();
  double Now();
  void Start();

  // Runs delayed closures as soon as possible.
  void Shutdown();

  // Requests to quit dispatch loop of the WorkerThread's thread, and terminate
  // the thread.
  void Quit();

  // Joins the WorkerThread's thread.  You must call Quit() before Join(), and
  // call Join() before destructing the WorkerThread.
  void Join();

  void ThreadMain() override;
  bool Dispatch();

  // Registers file descriptor fd in priority.
  SocketDescriptor* RegisterSocketDescriptor(
      ScopedSocket&& fd, WorkerThreadManager::Priority priority);
  ScopedSocket DeleteSocketDescriptor(SocketDescriptor* d);

  void RegisterPollEvent(SocketDescriptor* d, DescriptorPoller::EventType);
  void UnregisterPollEvent(SocketDescriptor* d, DescriptorPoller::EventType);
  void RegisterTimeoutEvent(SocketDescriptor* d);
  void UnregisterTimeoutEvent(SocketDescriptor* d);

  void RegisterPeriodicClosure(PeriodicClosureId id,
                               const char* const location,
                               int ms,
                               std::unique_ptr<PermanentClosure> closure);
  void UnregisterPeriodicClosure(PeriodicClosureId id,
                                 UnregisteredClosureData* data);

  void RunClosure(const char* const location,
                  Closure* closure, Priority priority);
  CancelableClosure* RunDelayedClosure(
      const char* const location,
      int msec, Closure* closure);

  size_t load() const;
  size_t pendings() const;

  bool IsIdle() const;
  string DebugString() const;

 private:
  struct ClosureData {
    ClosureData(const char* const location_,
                Closure* closure_,
                int queuelen,
                int tick,
                long long timestamp_ns);
    ClosureData();
    const char* location_;
    Closure* closure_;
    int queuelen_;
    int tick_;
    long long timestamp_ns_;
  };

  class CompareDelayedClosureImpl {
   public:
    bool operator()(DelayedClosureImpl* a, DelayedClosureImpl* b) const {
      return a->time() > b->time();
    }
  };
  typedef std::priority_queue<DelayedClosureImpl*,
                              std::vector<DelayedClosureImpl*>,
                              CompareDelayedClosureImpl> DelayedClosureQueue;

  // Forward declaration, actual prototype in worker_thread.cc.
  class PeriodicClosure;

  friend class WorkerThreadTest;

  // Updates current_closure_ to run if any.
  // Returns false if no closure to run now (no pending, no network I/O and
  // no timeout).
  bool NextClosure();

  // Adds closure in priority.
  // Assert mu_ held.
  void AddClosure(const char* const location,
                  WorkerThreadManager::Priority priority,
                  Closure* closure);

  // Gets closure in priority.
  // Assert mu_ held.
  ClosureData GetClosure(WorkerThreadManager::Priority priority);

  static void InitializeWorkerKey();

  WorkerThreadManager* wm_;
  int pool_;
  ThreadHandle handle_;
  ThreadId id_;
  ClosureData current_;
  SimpleTimer timer_;
  int tick_;
  long long now_ns_;
  bool shutting_down_;
  bool quit_;

  const std::string name_;

  mutable Lock mu_;
  ConditionVariable cond_handle_;  // signaled when handle_ is ready.
  ConditionVariable cond_id_;      // signaled when id_ is ready.
  // These auto_lock_stat_* are owned by g_auto_lock_stats.
  AutoLockStat* auto_lock_stat_next_closure_;
  AutoLockStat* auto_lock_stat_poll_events_;

  std::deque<ClosureData> pendings_[NUM_PRIORITIES];
  int max_queuelen_[NUM_PRIORITIES];
  long long max_wait_time_ns_[NUM_PRIORITIES];

  // delayed_pendings_ and periodic_closures_ are handled in PRIORITY_IMMEDIATE
  DelayedClosureQueue delayed_pendings_;
  std::vector<std::unique_ptr<PeriodicClosure>> periodic_closures_;

  std::map<int, SocketDescriptor*> descriptors_;
  std::unique_ptr<DescriptorPoller> poller_;
  int poll_interval_;

  static absl::once_flag key_worker_once_;
#ifndef _WIN32
  static pthread_key_t key_worker_;
#else
  static DWORD key_worker_;
#endif
  DISALLOW_COPY_AND_ASSIGN(WorkerThread);
};


}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_WORKER_THREAD_H_
