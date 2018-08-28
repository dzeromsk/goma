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
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "autolock_timer.h"
#include "basictypes.h"
#include "callback.h"
#include "descriptor_event_type.h"
#include "lockhelper.h"
#include "platform_thread.h"
#include "scoped_fd.h"
#include "simple_timer.h"

// Note: __LINE__ need to be wrapped twice to make its number string.
//       Otherwise, not a line number but string literal "__LINE__" would be
//       shown.
#define GOMA_WORKER_THREAD_STRINGFY(i) #i
#define GOMA_WORKER_THREAD_STR(i) GOMA_WORKER_THREAD_STRINGFY(i)
#define FROM_HERE __FILE__ ":" GOMA_WORKER_THREAD_STR(__LINE__)

namespace devtools_goma {

class AutoLockStat;
class DescriptorPoller;
class SocketDescriptor;

using PeriodicClosureId = int;
constexpr PeriodicClosureId kInvalidPeriodicClosureId = -1;

class WorkerThread : public PlatformThread::Delegate {
 public:
  // Windows often pass back 0xfffffffe (pseudo handle) as thread handle.
  // Therefore the reliable way of selecting a thread is to use the thread id.
  // ThreadHandle is used for Join().
  using ThreadHandle = PlatformThreadHandle;
  using ThreadId = PlatformThreadId;

  // We use SimpleTimer for monotonicity, which absl::Now() does not have. All
  // timestamps are given as durations since the start of the thread.
  using Timestamp = absl::Duration;

  // Priority of closures and descriptors.
  enum Priority {
    PRIORITY_MIN = 0,
    PRIORITY_LOW = 0,    // Used in compile_task.
    PRIORITY_MED,        // Used in http rpc and subprocess ipc.
    PRIORITY_HIGH,       // Used in http server (http and goma ipc serving)
    PRIORITY_IMMEDIATE,  // Called without descriptor polling.
                         // Used to clear notification closures of descriptor,
                         // delayed closures, or periodic closures.
    NUM_PRIORITIES
  };

  // Thread unsafe.  See RunDelayedClosureInThread.
  class CancelableClosure {
   public:
    CancelableClosure(const char* const locaction, Closure* closure);
    const char* location() const;
    void Cancel();
   protected:
    virtual ~CancelableClosure();
    Closure* closure_;
   private:
    const char* const location_;
    DISALLOW_COPY_AND_ASSIGN(CancelableClosure);
  };

  // See UnregisterPeriodicClosure
  class UnregisteredClosureData {
   public:
    UnregisteredClosureData() : done_(false), location_(nullptr) {}

    bool Done() const {
      AUTOLOCK(lock, &mu_);
      return done_;
    }
    void SetDone(bool b) {
      AUTOLOCK(lock, &mu_);
      done_ = b;
    }

    const char* Location() const {
      AUTOLOCK(lock, &mu_);
      return location_;
    }
    void SetLocation(const char* location) {
      AUTOLOCK(lock, &mu_);
      location_ = location;
    }

   private:
    mutable Lock mu_;
    bool done_ GUARDED_BY(mu_);
    const char* location_ GUARDED_BY(mu_);

    DISALLOW_COPY_AND_ASSIGN(UnregisteredClosureData);
  };

  class DelayedClosureImpl : public CancelableClosure {
   public:
    DelayedClosureImpl(const char* const location,
                       Timestamp t, Closure* closure)
        : CancelableClosure(location, closure), time_(t) {}
    Timestamp time() const { return time_; }
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

    Timestamp time_;
    DISALLOW_COPY_AND_ASSIGN(DelayedClosureImpl);
  };

  static void Initialize();
  static WorkerThread* GetCurrentWorker();

  WorkerThread(int pool, std::string name);
  ~WorkerThread() override;

  int pool() const { return pool_; }
  ThreadId id() const { return id_; }
  Timestamp NowCached();
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
      ScopedSocket&& fd, Priority priority);
  ScopedSocket DeleteSocketDescriptor(SocketDescriptor* d);

  void RegisterPollEvent(SocketDescriptor* d, DescriptorEventType);
  void UnregisterPollEvent(SocketDescriptor* d, DescriptorEventType);
  void RegisterTimeoutEvent(SocketDescriptor* d);
  void UnregisterTimeoutEvent(SocketDescriptor* d);

  void RegisterPeriodicClosure(PeriodicClosureId id,
                               const char* const location,
                               absl::Duration period,
                               std::unique_ptr<PermanentClosure> closure);
  void UnregisterPeriodicClosure(
      PeriodicClosureId id, UnregisteredClosureData* data);

  void RunClosure(const char* const location, Closure* closure,
                  Priority priority);
  CancelableClosure* RunDelayedClosure(
      const char* const location,
      absl::Duration delay, Closure* closure);

  size_t load() const;
  size_t pendings() const;

  bool IsIdle() const;
  string DebugString() const;

  static string Priority_Name(Priority priority);

 private:
  struct ClosureData {
    ClosureData(const char* const location,
                Closure* closure,
                int queuelen,
                int tick,
                Timestamp timestamp);
    const char* location_;
    Closure* closure_;
    int queuelen_;
    int tick_;
    Timestamp timestamp_;
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
                  Priority priority,
                  Closure* closure);

  // Gets closure in priority.
  // Assert mu_ held.
  ClosureData GetClosure(Priority priority);

  static void InitializeWorkerKey();

  int pool_;
  ThreadHandle handle_;
  ThreadId id_;
  absl::optional<ClosureData> current_closure_data_;
  SimpleTimer timer_;
  int tick_;
  absl::optional<Timestamp> now_cached_;
  bool shutting_down_;
  bool quit_;

  const std::string name_;

  mutable Lock mu_;
  ConditionVariable cond_id_;      // signaled when id_ is ready.
  // These auto_lock_stat_* are owned by g_auto_lock_stats.
  AutoLockStat* auto_lock_stat_next_closure_;
  AutoLockStat* auto_lock_stat_poll_events_;

  std::deque<ClosureData> pendings_[NUM_PRIORITIES];
  int max_queuelen_[NUM_PRIORITIES];
  absl::Duration max_wait_time_[NUM_PRIORITIES];

  // delayed_pendings_ and periodic_closures_ are handled in PRIORITY_IMMEDIATE
  DelayedClosureQueue delayed_pendings_;
  std::vector<std::unique_ptr<PeriodicClosure>> periodic_closures_;

  std::map<int, std::unique_ptr<SocketDescriptor>> descriptors_;
  std::unique_ptr<DescriptorPoller> poller_;
  absl::Duration poll_interval_;

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
