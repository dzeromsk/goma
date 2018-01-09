// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "subprocess_task.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include <sstream>
#include <string>
#include <vector>

#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "file.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()
#include "scoped_tmp_file.h"
#include "subprocess_controller_client.h"

namespace devtools_goma {

/* static */
string SubProcessTask::ReadCommandOutput(
    const string& prog,
    const std::vector<string>& argv, const std::vector<string>& envs,
    const string& cwd, CommandOutputOption option, int32_t* status) {
  CHECK(!SubProcessControllerClient::Get()->BelongsToCurrentThread());
  std::vector<const char*> args;
  for (const auto& arg : argv)
    args.push_back(arg.c_str());
  args.push_back(nullptr);

  SubProcessTask s(prog, prog.c_str(), const_cast<char**>(&args[0]));
  SubProcessReq* req = s.mutable_req();
  for (const auto& env : envs)
    req->add_env(env);
  if (cwd.empty()) {
    req->set_cwd(SubProcessControllerClient::Get()->tmp_dir());
  } else {
    req->set_cwd(cwd);
  }
  ScopedTmpFile tmpfile("goma_compiler_proxy.subproc");
  if (!tmpfile.valid()) {
    PLOG(ERROR) << "Failed to create tempfile to store stdout.";
    *status = SubProcessTerminated::kInternalError;
    return "";
  }
  tmpfile.Close();
  const string& tempfilename_stdout = tmpfile.filename();
  req->set_stdout_filename(tempfilename_stdout);
  if (option == STDOUT_ONLY)
    req->set_output_option(SubProcessReq::STDOUT_ONLY);

  req->set_priority(SubProcessReq::HIGHEST_PRIORITY);
  req->set_weight(SubProcessReq::LIGHT_WEIGHT);

  s.StartInternal(nullptr);  // blocking.
  string output;
  if (!ReadFileToString(tempfilename_stdout, &output)) {
    LOG(ERROR) << "Failed to read tempfile for storing stdout."
               << " tempfilename_stdout=" << tempfilename_stdout;
    *status = SubProcessTerminated::kInternalError;
    return "";
  }
  VLOG(3) << "output=" << output;
  int32_t exit_status = s.terminated().status();
  if (status) {
    *status = exit_status;
  } else {
    LOG_IF(FATAL, exit_status != 0)
        << "If the caller expects the non-zero exit status, "
        << "the caller must set non-nullptr status in the argument."
        << " prog=" << prog
        << " cwd=" << cwd
        << " exit_status=" << exit_status
        << " argv=" << argv;
  }
  return output;
}

SubProcessTask::SubProcessTask(
    const string& trace_id, const char* prog, char* const argv[])
    : thread_id_(0),
      callback_(nullptr),
      cond_(&mu_),
      state_(SubProcessState::SETUP) {
  DCHECK(SubProcessControllerClient::IsRunning());
  DCHECK(SubProcessControllerClient::Get()->Initialized());
  thread_id_ = SubProcessControllerClient::Get()->wm()->GetCurrentThreadId();
  VLOG(1) << trace_id << " new SubProcessTask";
  req_.set_id(-1);
  req_.set_trace_id(trace_id);
  req_.set_prog(prog);
  for (char* const* arg = argv; *arg != nullptr; ++arg) {
    req_.add_argv(*arg);
  }
  req_.set_priority(SubProcessReq::LOW_PRIORITY);
  req_.set_weight(SubProcessReq::LIGHT_WEIGHT);
}

SubProcessTask::~SubProcessTask() {
  VLOG(1) << req_.trace_id() << " delete";
  DCHECK(callback_ == nullptr);
  if (!req_.detach())
    DCHECK_EQ(SubProcessState::FINISHED, state_);
  if (SubProcessControllerClient::IsRunning())
    DCHECK(BelongsToCurrentThread());
}

bool SubProcessTask::BelongsToCurrentThread() const {
  return THREAD_ID_IS_SELF(thread_id_);
}

void SubProcessTask::Start(OneshotClosure* callback) {
  VLOG(1) << req_.trace_id() << " start";
  DCHECK(BelongsToCurrentThread());
  DCHECK_EQ(SubProcessState::SETUP, state_);
  DCHECK(!callback_);
  if (req_.detach())
    CHECK(callback == nullptr);
  else
    CHECK(callback != nullptr);
  StartInternal(callback);
}

void SubProcessTask::StartInternal(OneshotClosure* callback) {
  DCHECK(BelongsToCurrentThread());
  DCHECK_EQ(SubProcessState::SETUP, state_);
  DCHECK(!callback_);
  callback_ = callback;

  {
    AUTOLOCK(lock, &mu_);
    state_ = SubProcessState::PENDING;
  }
  SubProcessControllerClient::Get()->RegisterTask(this);
  if (req_.detach()) {
    CHECK(callback == nullptr);
    delete this;
    return;
  }
  if (callback == nullptr) {
    // blocking mode.
    AUTOLOCK(lock, &mu_);
    while (state_ != SubProcessState::FINISHED) {
      cond_.Wait();
    }
  }
}

void SubProcessTask::RequestRun() {
  VLOG(1) << req_.trace_id() << " request run ";
  DCHECK(BelongsToCurrentThread());
  std::unique_ptr<SubProcessRun> run;
  {
    AUTOLOCK(lock, &mu_);
    if (state_ == SubProcessState::SETUP) {
      LOG(FATAL) << req_.trace_id()
                 << " run in SETUP:" << req_.DebugString();
    }
    if (state_ != SubProcessState::PENDING) {
      VLOG(1) << req_.trace_id()
              << " run in not PENDING:" << req_.DebugString();
      return;
    }
    req_.set_priority(SubProcessReq::HIGH_PRIORITY);
    run.reset(new SubProcessRun);
    run->set_id(req_.id());
  }
  SubProcessControllerClient::Get()->RequestRun(std::move(run));
}

bool SubProcessTask::Kill() {
  VLOG(1) << req_.trace_id() << " kill";
  DCHECK(BelongsToCurrentThread());

  std::unique_ptr<SubProcessKill> kill;
  bool r = false;
  {
    AUTOLOCK(lock, &mu_);
    switch (state_) {
      case SubProcessState::SETUP:
        LOG(INFO) << req_.trace_id()
                  << " killed in SETUP:" << req_.DebugString();
        break;
      case SubProcessState::PENDING:
        state_ = SubProcessState::SIGNALED;
        kill.reset(new SubProcessKill);
        kill->set_id(req_.id());
        r = false;
        break;
      case SubProcessState::RUN:
        state_ = SubProcessState::SIGNALED;
        kill.reset(new SubProcessKill);
        kill->set_id(req_.id());
        r = true;
        break;
      case SubProcessState::SIGNALED:
        r = false;
        break;
      case SubProcessState::FINISHED:
        r = false;
        break;
      default:
        break;
    }
  }
  if (kill)
    SubProcessControllerClient::Get()->Kill(std::move(kill));
  return r;
}

/* static */
int SubProcessTask::NumPending() {
  return SubProcessControllerClient::Get()->NumPending();
}

void SubProcessTask::Started(std::unique_ptr<SubProcessStarted> started) {
  VLOG(1) << req_.trace_id() << " started " << started->pid();
  DCHECK(!BelongsToCurrentThread());
  {
    AUTOLOCK(lock, &mu_);
    if (state_ != SubProcessState::PENDING) {
      CHECK_EQ(SubProcessState::SIGNALED, state_)
          << req_.trace_id()
          << " state=" << SubProcessState::State_Name(state_)
          << started->DebugString();
    } else {
      state_ = SubProcessState::RUN;
    }
    started_ = *started;
  }
  LOG(INFO) << req_.trace_id() << " started pid=" << started_.pid()
            << " state=" << SubProcessState::State_Name(state_);
}

void SubProcessTask::Terminated(
    std::unique_ptr<SubProcessTerminated> terminated) {
  VLOG(1) << req_.trace_id() << " terminated " << terminated->status();
  DCHECK(!BelongsToCurrentThread());
  {
    AUTOLOCK(lock, &mu_);
    if (started_.pid() != SubProcessState::kInvalidPid) {
      LOG(INFO) << req_.trace_id() << " terminated pid=" << started_.pid()
                << " status=" << terminated->status();
    } else {
      VLOG(1) << req_.trace_id() << " subproc terminated";
    }
    terminated_ = *terminated;
    state_ = SubProcessState::FINISHED;
    cond_.Signal();
  }
}

void SubProcessTask::Done() {
  VLOG(1) << req_.trace_id() << " done";
  // SubProcessControllerClient might have been finished before calling
  // this method.
  if (SubProcessControllerClient::IsRunning())
    DCHECK(BelongsToCurrentThread());
  if (callback_) {
    OneshotClosure* callback = callback_;
    callback_ = nullptr;
    callback->Run();
  }
  delete this;
}

}  // namespace devtools_goma
