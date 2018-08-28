// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_WORKER_THREAD_MANAGER_H_
#define DEVTOOLS_GOMA_CLIENT_WORKER_THREAD_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "basictypes.h"
#include "lockhelper.h"
#include "platform_thread.h"
#include "worker_thread.h"

using std::string;

namespace devtools_goma {

class Closure;
class IOChannel;
class OneshotClosure;
class PermanentClosure;
class ScopedSocket;
class SocketDescriptor;
class WorkerThreadManagerTest;

class WorkerThreadManager {
 public:
  // Aliases for types that were moved to WorkerThread.
  using CancelableClosure = WorkerThread::CancelableClosure;
  using Priority = WorkerThread::Priority;
  using ThreadId = WorkerThread::ThreadId;

  // Default pool ids.
  enum {
    kDeadPool = -2,   // For terminated workers.
    kAlarmPool = -1,  // For periodic closures.
    kFreePool = 0,    // For RunClosure().
  };

  WorkerThreadManager();
  ~WorkerThreadManager();

  // Starts worker threads.
  void Start(int num_threads);

  // Starts pool of num_threads.  Returns pool id that can be used for
  // RunClosureInPool().
  // Can't be called on a worker thread.
  int StartPool(int num_threads, const std::string& name);

  // Starts new dedicated worker thread.
  void NewThread(OneshotClosure* closure, const std::string& name);

  size_t num_threads() const;

  // Shutdown. runs delayed closures as soon as possible.
  // Can't be called on a worker thread.
  void Shutdown();

  // Finishes all workers.
  // Can't be called on a worker thread.
  void Finish();

  ThreadId GetCurrentThreadId();

  // Run one step in current worker thread.
  // Returns true if the worker thread is active.
  // Returns false if the worker thread is terminating.
  bool Dispatch();

  // Registers file descriptor in current worker thread.
  SocketDescriptor* RegisterSocketDescriptor(
      ScopedSocket&& fd, Priority priority);
  ScopedSocket DeleteSocketDescriptor(SocketDescriptor* d);

  // Registers periodic closure.
  PeriodicClosureId RegisterPeriodicClosure(
      const char* const location,
      absl::Duration period,
      std::unique_ptr<PermanentClosure> closure);

  // Unregisters periodic closure.
  void UnregisterPeriodicClosure(PeriodicClosureId id);

  // Runs closure on least loaded worker thread in kFreePool.
  void RunClosure(const char* const location,
                  Closure* closure, Priority priority);

  // Runs closure in pool, which was created by StartPool().
  void RunClosureInPool(const char* const location,
                        int pool,
                        Closure* closure,
                        Priority priority);

  // Runs closure on specified worker thread.
  void RunClosureInThread(const char* const location,
                          ThreadId id, Closure* closure,
                          Priority priority);

  // Runs closure after msec on specified worker thread.
  // It takes onwership of closure. It will be deleted if it is canceled.
  // Normal closure will be deleted when it runs, so just pass ownership
  // of the closure.
  // Permanent closure won't be deleted when it runs, so it would be
  // difficult to tell who is the owner of the closure; thus, don't pass
  // permanent closure to this.
  // CancelableClosure will be valid until closure returns, or
  // Cancel is called.
  // CancelableClosure is thread unsafe.  Access it only in the specified
  // worker thread.
  CancelableClosure* RunDelayedClosureInThread(const char* const location,
                                               ThreadId handle,
                                               absl::Duration delay,
                                               Closure* closure);

  string DebugString() const;
  void DebugLog() const;

 private:
  friend class WorkerThreadManagerTest;
  struct Periodic;

  static void RegisterPeriodicClosureOnAlarmer(
      WorkerThread* alarmer, PeriodicClosureId id, const char* location,
      absl::Duration period, std::unique_ptr<PermanentClosure> closure);

  WorkerThread* GetWorker(ThreadId id);
  WorkerThread* GetWorkerUnlocked(ThreadId id) SHARED_LOCKS_REQUIRED(mu_);
  WorkerThread* GetCurrentWorker();

  PeriodicClosureId NextPeriodicClosureId();

  mutable ReadWriteLock mu_;
  std::vector<WorkerThread*> workers_ GUARDED_BY(mu_);
  size_t next_worker_index_ GUARDED_BY(mu_);
  int next_pool_ GUARDED_BY(mu_);

  WorkerThread* alarm_worker_;

  Lock periodic_closure_id_mu_;
  PeriodicClosureId next_periodic_closure_id_
      GUARDED_BY(periodic_closure_id_mu_);

  DISALLOW_COPY_AND_ASSIGN(WorkerThreadManager);
};

// WorkerThreadRunner runs closure in worker thread manager.
// It will wait for closure completion before it is destructed.
class WorkerThreadRunner {
 public:
  WorkerThreadRunner(WorkerThreadManager* wm,
                     const char* const location,
                     OneshotClosure* closure);
  ~WorkerThreadRunner();

  void Wait();
  bool Done() const;

 private:
  void Run(OneshotClosure* closure);

  mutable Lock mu_;
  ConditionVariable cond_;
  bool done_ GUARDED_BY(mu_);

  DISALLOW_COPY_AND_ASSIGN(WorkerThreadRunner);
};


}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_WORKER_THREAD_MANAGER_H_
