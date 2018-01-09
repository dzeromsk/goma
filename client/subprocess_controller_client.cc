// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "subprocess_controller_client.h"

#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#else
#include "config_win.h"
#include "socket_helper_win.h"
#endif

#include <memory>
#include <sstream>
#include <vector>

#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "socket_descriptor.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()
#include "subprocess_task.h"
#include "worker_thread_manager.h"

using std::string;

namespace devtools_goma {

SubProcessControllerClient *gSubProcessController;

/* static */
SubProcessControllerClient* SubProcessControllerClient::Create(
    int fd, pid_t pid, const Options& options) {
  // Must be called before starting threads.
  gSubProcessController = new SubProcessControllerClient(fd, pid, options);
  CHECK(gSubProcessController != nullptr);
  return gSubProcessController;
}

/* static */
bool SubProcessControllerClient::IsRunning() {
  return gSubProcessController != nullptr;
}

/* static */
SubProcessControllerClient* SubProcessControllerClient::Get() {
  CHECK(gSubProcessController != nullptr);
  return gSubProcessController;
}

/* static */
void SubProcessControllerClient::Initialize(
    WorkerThreadManager* wm, const string& tmp_dir) {
  wm->NewThread(
      NewCallback(
          Get(), &SubProcessControllerClient::Setup,
          wm, tmp_dir), "subprocess_controller_client");
}

SubProcessControllerClient::SubProcessControllerClient(int fd,
                                                       pid_t pid,
                                                       const Options& options)
    : wm_(nullptr),
      thread_id_(0),
      d_(nullptr),
      fd_(fd),
      server_pid_(pid),
      cond_(&mu_),
      next_id_(0),
      current_options_(options),
      periodic_closure_id_(kInvalidPeriodicClosureId),
      quit_(false),
      initialized_(false) {
}

SubProcessControllerClient::~SubProcessControllerClient() {
  CHECK(quit_);
  CHECK(subproc_tasks_.empty());
  CHECK_EQ(periodic_closure_id_, kInvalidPeriodicClosureId);
  ScopedSocket fd(wm_->DeleteSocketDescriptor(d_));
  fd.Close();
  d_ = nullptr;
  thread_id_ = 0;
  wm_ = nullptr;
  gSubProcessController = nullptr;
}

void SubProcessControllerClient::Setup(
    WorkerThreadManager* wm, string tmp_dir) {
  wm_ = wm;
  thread_id_ = wm_->GetCurrentThreadId();
  d_ = wm_->RegisterSocketDescriptor(std::move(fd_),
                                     WorkerThreadManager::PRIORITY_MED);
  SetInitialized();
  d_->NotifyWhenReadable(
      NewPermanentCallback(this, &SubProcessControllerClient::DoRead));
  tmp_dir_ = tmp_dir;
  {
    AUTOLOCK(lock, &mu_);
    CHECK_EQ(periodic_closure_id_, kInvalidPeriodicClosureId);
    periodic_closure_id_ = wm_->RegisterPeriodicClosure(
        FROM_HERE, 10 * 1000, NewPermanentCallback(
            this, &SubProcessControllerClient::RunCheckSignaled));
  }
  LOG(INFO) << "SubProcessControllerClient Initialized fd=" << d_->fd();
}

void SubProcessControllerClient::SetInitialized() {
  AUTOLOCK(lock, &initialized_mu_);
  initialized_ = true;
}

bool SubProcessControllerClient::Initialized() const {
  AUTOLOCK(lock, &initialized_mu_);
  return initialized_;
}

void SubProcessControllerClient::Quit() {
  LOG(INFO) << "SubProcessControllerClient Quit";

  std::vector<std::unique_ptr<SubProcessKill>> kills;
  {
    AUTOLOCK(lock, &mu_);
    quit_ = true;
    for (std::map<int, SubProcessTask*>::iterator iter = subproc_tasks_.begin();
         iter != subproc_tasks_.end();
         ++iter) {
      std::unique_ptr<SubProcessKill> kill(new SubProcessKill);
      kill->set_id(iter->first);
      kills.emplace_back(std::move(kill));
    }
  }
  for (size_t i = 0; i < kills.size(); ++i) {
    Kill(std::move(kills[i]));
  }
  {
    AUTOLOCK(lock, &mu_);
    if (periodic_closure_id_ != kInvalidPeriodicClosureId) {
      wm_->UnregisterPeriodicClosure(periodic_closure_id_);
      periodic_closure_id_ = kInvalidPeriodicClosureId;
    }
  }
}

void SubProcessControllerClient::Shutdown() {
  LOG(INFO) << "SubProcessControllerClient shutdown";
  {
    AUTOLOCK(lock, &mu_);
    CHECK(quit_);
    CHECK_EQ(periodic_closure_id_, kInvalidPeriodicClosureId);
    while (!subproc_tasks_.empty()) {
      LOG(INFO) << "wait for subproc_tasks_ become empty";
      cond_.Wait();
    }
  }
  // Not to pass SubProcessControllerClient::SendRequest to send Kill,
  // this should be executed with PRIORITY_MED.
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      NewCallback(
          this, &SubProcessControllerClient::Delete),
      WorkerThreadManager::PRIORITY_MED);
}

void SubProcessControllerClient::RegisterTask(SubProcessTask* task) {
  CHECK_EQ(-1, task->req().id()) << task->req().DebugString();
  CHECK_EQ(SubProcessState::PENDING, task->state())
      << task->req().DebugString();
  int id = 0;
  bool quit = false;
  {
    AUTOLOCK(lock, &mu_);
    if (quit_) {
      quit = true;
      // don't put in subproc_tasks_.
    } else {
      id = ++next_id_;
      // detach task would not notify back, so no need to set it
      // in subproc_tasks_.
      if (!task->req().detach()) {
        subproc_tasks_.insert(std::make_pair(id, task));
      }
    }
  }
  if (quit) {
    LOG(INFO) << task->req().trace_id() << ": RegisterTask in quit";
    std::unique_ptr<SubProcessTerminated> terminated(new SubProcessTerminated);
    terminated->set_id(id);
    terminated->set_status(SubProcessTerminated::kNotStarted);
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        devtools_goma::NewCallback(
            task, &SubProcessTask::Terminated, std::move(terminated)),
        WorkerThreadManager::PRIORITY_MED);
    return;
  }
  VLOG(1) << task->req().trace_id() << ": RegisterTask id=" << id;
  task->mutable_req()->set_id(id);
  std::unique_ptr<SubProcessReq> req(new SubProcessReq);
  *req = task->req();
  Register(std::move(req));
}

void SubProcessControllerClient::Register(std::unique_ptr<SubProcessReq> req) {
  {
    AUTOLOCK(lock, &mu_);
    if (quit_)
      return;
  }
  VLOG(1) << "Register id=" << req->id() << " " << req->trace_id();
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      devtools_goma::NewCallback(
          this, &SubProcessControllerClient::SendRequest,
          SubProcessController::REGISTER,
          std::unique_ptr<google::protobuf::Message>(std::move(req))),
      WorkerThreadManager::PRIORITY_MED);
}

void SubProcessControllerClient::RequestRun(
    std::unique_ptr<SubProcessRun> run) {
  VLOG(1) << "Run id=" << run->id();
  {
    AUTOLOCK(lock, &mu_);
    if (quit_)
      return;
  }
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      devtools_goma::NewCallback(
          this, &SubProcessControllerClient::SendRequest,
          SubProcessController::REQUEST_RUN,
          std::unique_ptr<google::protobuf::Message>(std::move(run))),
      WorkerThreadManager::PRIORITY_MED);
}

void SubProcessControllerClient::Kill(std::unique_ptr<SubProcessKill> kill) {
  {
    AUTOLOCK(lock, &mu_);
    if (periodic_closure_id_ == kInvalidPeriodicClosureId) {
      return;
    }
  }
  LOG(INFO) << "Kill id=" << kill->id();
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      devtools_goma::NewCallback(
          this, &SubProcessControllerClient::SendRequest,
          SubProcessController::KILL,
          std::unique_ptr<google::protobuf::Message>(std::move(kill))),
      WorkerThreadManager::PRIORITY_MED);
}

void SubProcessControllerClient::SetOption(
    std::unique_ptr<SubProcessSetOption> option) {
  {
    AUTOLOCK(lock, &mu_);
    if (periodic_closure_id_ == kInvalidPeriodicClosureId) {
      return;
    }

    current_options_.max_subprocs = option->max_subprocs();
    current_options_.max_subprocs_low_priority =
        option->max_subprocs_low_priority();
    current_options_.max_subprocs_heavy_weight =
        option->max_subprocs_heavy_weight();
  }
  LOG(INFO) << "SetOption"
            << " max_subprocs=" << option->max_subprocs()
            << " max_subprocs_heavy_weight="
            << option->max_subprocs_heavy_weight()
            << " max_subprocs_low_priority="
            << option->max_subprocs_low_priority();
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      devtools_goma::NewCallback(
          this, &SubProcessControllerClient::SendRequest,
          SubProcessController::SET_OPTION,
          std::unique_ptr<google::protobuf::Message>(std::move(option))),
      WorkerThreadManager::PRIORITY_MED);
}

void SubProcessControllerClient::Started(
    std::unique_ptr<SubProcessStarted> started) {
  VLOG(1) << "Started " << started->id() << " pid=" << started->pid();
  DCHECK(BelongsToCurrentThread());
  int id = started->id();
  SubProcessTask* task = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    std::map<int, SubProcessTask*>::iterator found =
        subproc_tasks_.find(id);
    if (found != subproc_tasks_.end()) {
      task = found->second;
    }
  }
  if (task == nullptr) {
    LOG(WARNING) << "No task for id=" << id;
    std::unique_ptr<SubProcessKill> kill(new SubProcessKill);
    kill->set_id(id);
    Kill(std::move(kill));
    return;
  }
  task->Started(std::move(started));
}

void SubProcessControllerClient::Terminated(
    std::unique_ptr<SubProcessTerminated> terminated) {
  DCHECK(BelongsToCurrentThread());
  VLOG(1) << "Terminated " << terminated->id()
          << " status=" << terminated->status();
  int id = terminated->id();
  SubProcessTask* task = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    std::map<int, SubProcessTask*>::iterator found =
        subproc_tasks_.find(id);
    if (found != subproc_tasks_.end()) {
      task = found->second;
      subproc_tasks_.erase(found);
    }
  }
  if (task != nullptr) {
    bool async = task->async_callback();
    task->Terminated(std::move(terminated));
    // If task is synchronous (!async), task may already be deleted here.
    if (async) {
      wm_->RunClosureInThread(
          FROM_HERE,
          task->thread_id(),
          NewCallback(
              task, &SubProcessTask::Done),
          WorkerThreadManager::PRIORITY_MED);
    }
  } else {
    std::ostringstream ss;
    ss << "no task found for id=" << id
       << " status=" << terminated->status()
       << " error=" << SubProcessTerminated_ErrorTerminate_Name(
           terminated->error());
    if (terminated->error() == SubProcessTerminated::kFailedToLookup) {
      LOG(INFO) << ss.str();
    } else {
      LOG(WARNING) << ss.str();
    }
  }

  {
    AUTOLOCK(lock, &mu_);
    if (quit_ && subproc_tasks_.empty()) {
      LOG(INFO) << "all subproc_tasks done";
      d_->StopRead();
      d_->StopWrite();
      CHECK(subproc_tasks_.empty());
      cond_.Signal();
    }
  }
}

int SubProcessControllerClient::NumPending() const {
  AUTOLOCK(lock, &mu_);
  int num_pending = 0;
  for (std::map<int, SubProcessTask*>::const_iterator iter =
           subproc_tasks_.begin();
       iter != subproc_tasks_.end();
       ++iter) {
    SubProcessTask* task = iter->second;
    switch (task->state()) {
      case SubProcessState::SETUP: case SubProcessState::PENDING:
        ++num_pending;
        break;
      default:
        { }
    }
  }
  return num_pending;
}

bool SubProcessControllerClient::BelongsToCurrentThread() const {
  return THREAD_ID_IS_SELF(thread_id_);
}

void SubProcessControllerClient::Delete() {
  DCHECK(BelongsToCurrentThread());
  d_->ClearReadable();
  delete this;
}

void SubProcessControllerClient::SendRequest(
    SubProcessController::Op op,
    std::unique_ptr<google::protobuf::Message> message) {
  DCHECK(BelongsToCurrentThread());
  if (AddMessage(op, *message)) {
    VLOG(3) << "SendRequest has pending write";
    d_->NotifyWhenWritable(
        NewPermanentCallback(this, &SubProcessControllerClient::DoWrite));
  }
}

void SubProcessControllerClient::DoWrite() {
  VLOG(2) << "DoWrite";
  DCHECK(BelongsToCurrentThread());
  if (!WriteMessage(d_->wrapper())) {
    VLOG(3) << "DoWrite no pending";
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        NewCallback(
            this, &SubProcessControllerClient::WriteDone),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
  }
}

void SubProcessControllerClient::WriteDone() {
  VLOG(2) << "WriteDone";
  DCHECK(BelongsToCurrentThread());
  if (has_pending_write())
    return;
  d_->ClearWritable();
}

void SubProcessControllerClient::DoRead() {
  VLOG(2) << "DoRead";
  DCHECK(BelongsToCurrentThread());
  int op = 0;
  int len = 0;
  if (!ReadMessage(d_->wrapper(), &op, &len)) {
    VLOG(2) << "pending read op=" << op << " len=" << len;
    return;
  }
  VLOG(2) << "DoRead op=" << op << " len=" << len;
  switch (op) {
    case SubProcessController::CLOSED:
#ifndef _WIN32
      LOG(ERROR) << "SubProcessControllerServer died unexpectedly."
                 << " pid=" << server_pid_;
      {
        // subprocess controller server process was killed or crashed?
        int status = 0;
        if (waitpid(server_pid_, &status, 0) == -1) {
          PLOG(FATAL) << "SubProcessControllerServer wait failed pid="
                      << server_pid_;
        }
        int exit_status = WEXITSTATUS(status);
        int signaled = 0;
        if (WIFSIGNALED(status)) {
          signaled = WTERMSIG(status);
        }
        LOG(INFO) << "SubProcessControllerServer exited "
                  << " status=" << exit_status
                  << " signal=" << signaled;
        if (exit_status != 0 && signaled != 0) {
          LOG(FATAL) << "unexpected SubProcessControllerServer exit";
        }
      }
      exit(0);
#else
      // subprocess controller server is a thread, not a process on Windows.
      LOG(FATAL) << "SubProcessControllerServer died unexpectedly.";
#endif

    // Note: STARTED and TERMINATED should run closure with the same priority
    // Otherwise, they may not be executed in order.
    case SubProcessController::STARTED: {
        std::unique_ptr<SubProcessStarted> started(new SubProcessStarted);
        if (started->ParseFromArray(payload_data(), len)) {
          wm_->RunClosureInThread(
              FROM_HERE,
              thread_id_,
              devtools_goma::NewCallback(
                  this, &SubProcessControllerClient::Started,
                  std::move(started)),
              WorkerThreadManager::PRIORITY_MED);
        } else {
          LOG(ERROR) << "broken SubProcessStarted";
        }
      }
      break;

    case SubProcessController::TERMINATED: {
        std::unique_ptr<SubProcessTerminated> terminated(
            new SubProcessTerminated);
        if (terminated->ParseFromArray(payload_data(), len)) {
          wm_->RunClosureInThread(
              FROM_HERE,
              thread_id_,
              devtools_goma::NewCallback(
                  this, &SubProcessControllerClient::Terminated,
                  std::move(terminated)),
              WorkerThreadManager::PRIORITY_MED);
        } else {
          LOG(ERROR) << "broken SubProcessTerminated";
        }
      }
      break;

    default:
      LOG(FATAL) << "Unknown SubProcessController::Op " << op;
  }
  ReadDone();
  return;
}

void SubProcessControllerClient::RunCheckSignaled() {
  if (gSubProcessController == nullptr) {
    // RunCheckSignaled is periodic closure managed by gSubProcessController,
    // it should never be called when gSubProcessController == nullptr.
    LOG(FATAL) << "gSubProcessController is nullptr";
    return;
  }
  // Switch from alarm worker to client thread.
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      NewCallback(
          this, &SubProcessControllerClient::CheckSignaled),
      WorkerThreadManager::PRIORITY_MED);
}

void SubProcessControllerClient::CheckSignaled() {
  if (gSubProcessController == nullptr) {
    // gSubProcessController (and this pointer) may be nullptr because Delete is
    // higher priority (put in WorkerThreadManager in Shutdown).
    // Should not access any member fields here.
    return;
  }
  DCHECK(BelongsToCurrentThread());
  std::vector<std::unique_ptr<SubProcessKill>> kills;
  {
    AUTOLOCK(lock, &mu_);
    for (std::map<int, SubProcessTask*>::const_iterator iter =
             subproc_tasks_.begin();
         iter != subproc_tasks_.end();
         ++iter) {
      int id = iter->first;
      SubProcessTask* task = iter->second;
      if (task->state() == SubProcessState::SIGNALED) {
        std::unique_ptr<SubProcessKill> kill(new SubProcessKill);
        kill->set_id(id);
        kills.emplace_back(std::move(kill));
      }
    }
  }
  if (!kills.empty()) {
    for (size_t i = 0; i < kills.size(); ++i) {
      Kill(std::move(kills[i]));
    }
  }
}

string SubProcessControllerClient::DebugString() const {
  AUTOLOCK(lock, &mu_);
  std::ostringstream ss;

  ss << "options: " << current_options_.DebugString() << '\n';

  for (std::map<int, SubProcessTask*>::const_iterator iter =
           subproc_tasks_.begin();
       iter != subproc_tasks_.end();
       ++iter) {
    int id = iter->first;
    SubProcessTask* task = iter->second;
    ss << id << " "
       << task->req().trace_id() << " "
       << SubProcessState::State_Name(task->state()) << " "
       << SubProcessReq::Priority_Name(task->req().priority()) << " "
       << SubProcessReq::Weight_Name(task->req().weight()) << " "
       << "pid=" << task->started().pid() << "\n";
  }
  return ss.str();
}

}  // namespace devtools_goma
