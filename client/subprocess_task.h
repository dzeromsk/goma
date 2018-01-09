// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SUBPROCESS_TASK_H_
#define DEVTOOLS_GOMA_CLIENT_SUBPROCESS_TASK_H_

#include <memory>
#include <string>
#include <vector>

#include "basictypes.h"
#include "compiler_specific.h"
#include "lockhelper.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()
#include "util.h"
#include "worker_thread_manager.h"

using std::string;

namespace devtools_goma {

class Closure;
class SubProcessControllerClient;

// A SubProcessTask is managed by SubProcessControllerClient and is
// a peer of SubProcessImpl that is managed by SubProcessControllerServer.
// Typical usage is:
//     SubProcessTask* task = new SubProcessTask(trace_id, prog, argv);
//     SubProcessReq* req = task->mutable_req();
//     // setup request in req.
//     task->Start(callback);
//     // SubProcessControllerClient takes ownership of task.
//     // you can access task until callback is called.
//     // task->status(), task->started().pid(), ...
//  Once callback is called, the subprocess is terminated, and
//  the SubProcessTask will be deleted after returning the callback.
class SubProcessTask {
 public:
  // Provides ReadCommandOutput interface.
  // It uses SubProcessTask blocking mode internally.
  // |status| basically shows exit status of the program.
  // Since program exit status is usually positive value on Posix and Windows,
  // ReadCommandOutput set SubProcessTerminated::kInternalError to |status|
  // for its internal error.
  static string ReadCommandOutput(
      const string& prog,
      const std::vector<string>& argv, const std::vector<string>& env,
      const string& cwd, CommandOutputOption option, int32_t* status);

  // Creates new sub process task.
  // The created instance will be used on the thread where it was created.
  SubProcessTask(const string& trace_id,
                 const char* prog, char* const argv[]);

  WorkerThreadManager::ThreadId thread_id() { return thread_id_; }

  SubProcessState::State state() const {
    AUTOLOCK(lock, &mu_);
    return state_;
  }

  // Client can set subprocess configuration via mutable_req().
  // Must be called before Start() call.
  SubProcessReq* mutable_req() { return &req_; }

  const SubProcessReq& req() const { return req_; }
  const SubProcessStarted& started() const { return started_; }
  const SubProcessTerminated& terminated() const { return terminated_; }

  // Starts subprocess.
  // It returns immediately and callback will be called when the process is
  // finished, and delete it by itself.
  // If req().detach() is true, callback must be NULL.  The process will run
  // in detached mode.  SubProcessTask will be deleted before returning from
  // Start().
  // state(): SETUP -> PENDING.
  void Start(OneshotClosure* callback);

  // Requests to run the subprocess in high priority.
  void RequestRun();

  // Kills the subprocess. callback will be called when the process is killed.
  // state(): PENDING, RUN -> SIGNALED: returns true
  // state(): SIGNALED, TERMINATED -> returns false.
  bool Kill();

  static int NumPending();

 private:
  friend class SubProcessControllerClient;
  ~SubProcessTask();
  bool BelongsToCurrentThread() const;
  bool async_callback() const { return callback_ != NULL; }

  // Starts subprocess.
  // If callback is not NULL, it returns immediately and callback will be
  // called when the process is finished, and delete it by itself.
  // state(): SETUP -> PENDING.
  //
  // If callback is NULL, it waits for subprocess termination.
  // state(): SETUP -> .. -> FINISHED.
  // Caller should delete it.
  void StartInternal(OneshotClosure* callback);

  // Feedback from subprocess controller.

  // The subprocess is started with pid.
  // Runs in subprocess controller's context.
  // Takes ownership of started.
  void Started(std::unique_ptr<SubProcessStarted> started);

  // The subprocess is terminated.
  // Runs in subprocess controller's context.
  // Takes ownership of terminated.
  void Terminated(std::unique_ptr<SubProcessTerminated> terminated);

  // Calls callback_ and delete itself.
  void Done();

  SubProcessReq req_;
  SubProcessStarted started_;
  SubProcessTerminated terminated_;

  WorkerThreadManager::ThreadId thread_id_;

  OneshotClosure* callback_;

  Lock mu_;  // protect state_.
  ConditionVariable cond_;
  SubProcessState::State state_;

  DISALLOW_COPY_AND_ASSIGN(SubProcessTask);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SUBPROCESS_TASK_H_
