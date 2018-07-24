// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "subprocess_impl.h"

#include <vector>

#include "absl/memory/memory.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#ifndef _WIN32
#include "spawner_posix.h"
#else
#include "spawner_win.h"
#endif

namespace devtools_goma {

SubProcessImpl::SubProcessImpl(const SubProcessReq& req,
                               bool dont_kill_subprocess)
    : state_(SubProcessState::PENDING),
      spawner_(new PlatformSpawner),
      kill_subprocess_(!dont_kill_subprocess) {
  VLOG(1) << "new SubProcessImpl " << req.id()
          << " " << req.trace_id();
  req_ = req;
  VLOG(2) << "new " << req_.DebugString();
}

SubProcessImpl::~SubProcessImpl() {
  VLOG(1) << "delete SubProcessImpl " << req_.id()
          << " " << req_.trace_id();
  VLOG(2) << "delete " << req_.DebugString();
}

SubProcessStarted* SubProcessImpl::Spawn() {
  LOG(INFO) << "id=" << req_.id() << " spawn " << req_.trace_id();
  DCHECK_EQ(SubProcessState::PENDING, state_);
  DCHECK_EQ(SubProcessState::kInvalidPid, started_.pid());

  started_.set_pending_ms(timer_.GetInIntMilliseconds());

  std::vector<string> args(req_.argv().begin(), req_.argv().end());
  std::vector<string> envs;
  for (const auto& env : req_.env())
    envs.push_back(env.c_str());

  Spawner::ConsoleOutputOption output_option =
      Spawner::MERGE_STDOUT_STDERR;
  if (req_.output_option() == SubProcessReq::STDOUT_ONLY)
    output_option = Spawner::STDOUT_ONLY;
  spawner_->SetFileRedirection(req_.stdin_filename(),
                               req_.stdout_filename(),
                               req_.stderr_filename(),
                               output_option);
  spawner_->SetDetach(req_.detach());
  spawner_->SetKeepEnv(req_.keep_env());
  if (req_.has_umask()) {
    spawner_->SetUmask(req_.umask());
  }
  VLOG(1) << "id=" << req_.id()
          << " to_spawn " << req_.trace_id()
          << " prog=" << req_.prog()
          << " args=" << args
          << " envs=" << envs
          << " cwd=" << req_.cwd();
  int pid = spawner_->Run(req_.prog(), args, envs, req_.cwd());
  if (pid == Spawner::kInvalidPid) {
    LOG(ERROR) << "id=" << req_.id() << " spawn " << req_.trace_id()
               << " failed";
    return nullptr;
  }
  started_.set_pid(pid);
  timer_.Start();
  state_ = SubProcessState::RUN;
  started_.set_id(req_.id());
  if (req_.detach())
    return nullptr;
  SubProcessStarted* started = new SubProcessStarted;
  *started = started_;
  return started;
}

void SubProcessImpl::RaisePriority() {
  LOG(INFO) << "id=" << req_.id() << " Run " << req_.trace_id();
  req_.set_priority(SubProcessReq::HIGH_PRIORITY);
}

bool SubProcessImpl::Kill() {
  if (started_.pid() == SubProcessState::kInvalidPid) {
    LOG(INFO) << "id=" << req_.id() << " Kill before run "
              << req_.trace_id();
    return false;
  }

  bool running = spawner_->IsChildRunning();
  if (kill_subprocess_) {
    LOG(INFO) << "id=" << req_.id() << " kill " << req_.trace_id()
              << " pid=" << started_.pid()
              << " child_signaled=" << spawner_->IsSignaled()
              << " running=" << running;
    return spawner_->Kill() == Spawner::ProcessStatus::RUNNING;
  }
  LOG(INFO) << "id=" << req_.id() << " ignore kill " << req_.trace_id()
            << " pid=" << started_.pid() << " running=" << running;
  return running;
}

void SubProcessImpl::Signaled(int status) {
  LOG(INFO) << "id=" << req_.id() << " Signaled " << req_.trace_id()
            << " pid=" << started_.pid()
            << " status=" << status;
  spawner_->SetSignaled();
  terminated_.set_status(status);
  state_ = SubProcessState::SIGNALED;
}

std::unique_ptr<SubProcessTerminated> SubProcessImpl::Wait(bool need_kill) {
  VLOG(1) << "Wait " << req_.id() << " " << req_.trace_id()
          << " pid=" << started_.pid()
          << " state=" << SubProcessState::State_Name(state_);

  if (need_kill) {
    spawner_->Wait(Spawner::NEED_KILL);
  } else {
    spawner_->Wait(Spawner::NO_HANG);
  }
  if (spawner_->IsChildRunning()) {
    // still running.
    return nullptr;
  }
  terminated_.set_status(spawner_->ChildStatus());
  if (spawner_->ChildMemKb() > 0)
    terminated_.set_mem_kb(spawner_->ChildMemKb());
  if (spawner_->ChildTermSignal() != 0)
    terminated_.set_term_signal(spawner_->ChildTermSignal());
  terminated_.set_run_ms(timer_.GetInIntMilliseconds());

  state_ = SubProcessState::FINISHED;
  VLOG(1) << "Terminated " << req_.id() << " " << req_.trace_id()
          << " pid=" << started_.pid();
  terminated_.set_id(req_.id());
  auto terminated = absl::make_unique<SubProcessTerminated>();
  *terminated = terminated_;
  return terminated;
}

}  // namespace devtools_goma
