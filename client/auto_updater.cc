// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "auto_updater.h"

#include <vector>

#include "autolock_timer.h"
#include "callback.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "ioutil.h"
#include "mypath.h"
#include "path.h"
#include "subprocess_task.h"
#include "threadpool_http_server.h"

#ifdef _WIN32
#include "posix_helper_win.h"
#endif

namespace devtools_goma {

AutoUpdater::AutoUpdater(const string& goma_ctl)
    : dir_(GetMyDirectory()),
      my_version_(-1),
      pulled_version_(-1),
      idle_counter_(-1),
      server_(nullptr),
      pull_closure_id_(ThreadpoolHttpServer::kInvalidClosureId),
      subproc_(nullptr),
      goma_ctl_(goma_ctl) {
  ReadManifest(file::JoinPath(dir_, "MANIFEST"), &my_version_);
}

AutoUpdater::~AutoUpdater() {
  Stop();
  Wait();
}

void AutoUpdater::SetEnv(const char* envp[]) {
  for (const char** p = envp; *p != nullptr; ++p) {
    env_.push_back(*p);
  }
}

void AutoUpdater::Start(ThreadpoolHttpServer* server, int count) {
  if (my_version_ <= 0) {
    LOG(INFO) << "no goma version, disable auto update";
    return;
  }
  if (count <= 0) {
    LOG(INFO) << "disable auto_updater.";
    return;
  }
  if (access(file::JoinPath(dir_, "no_auto_update").c_str(), R_OK) == 0) {
    LOG(INFO) << "no_auto_update exists, disable auto update";
    return;
  }
  LOG(INFO) << "start autoupdate in " << count << " idle count.";
  server_ = server;
  idle_counter_ = count;
  std::unique_ptr<PermanentClosure> pull_closure(
      NewPermanentCallback(this, &AutoUpdater::CheckUpdate));
  pull_closure_id_ = server_->RegisterIdleClosure(
      ThreadpoolHttpServer::SOCKET_IPC, count, std::move(pull_closure));
}

void AutoUpdater::Stop() {
  if (server_) {
    server_->UnregisterIdleClosure(pull_closure_id_);
  }
  pull_closure_id_ = ThreadpoolHttpServer::kInvalidClosureId;
  server_ = nullptr;

  AUTOLOCK(lock, &mu_);
  if (subproc_) {
    subproc_->Kill();
  }
}

void AutoUpdater::Wait() {
  AUTOLOCK(lock, &mu_);
  while (subproc_ != nullptr) {
    cond_.Wait(&mu_);
  }
}

bool AutoUpdater::ReadManifest(const string& path, int* version) {
  string manifest;
  if (!ReadFileToString(path.c_str(), &manifest))
    return false;
  static const int kVersionLen = strlen("VERSION=");
  size_t version_start = manifest.find("VERSION=");
  if (version_start == string::npos)
    return false;
  size_t version_end = manifest.find("\n", version_start + kVersionLen);
  if (version_end == string::npos)
    return false;
  string version_str = manifest.substr(
      version_start + kVersionLen,
      version_end - (version_start + kVersionLen));
  *version = atoi(version_str.c_str());
  LOG(INFO) << "manifest " << path << " VERSION=" << *version;
  return (*version > 0);
}

void AutoUpdater::CheckUpdate() {
  {
    AUTOLOCK(lock, &mu_);
    if (subproc_ != nullptr)
      return;
    if (server_ == nullptr)
      return;
  }
  int last_idle_counter =
      server_->idle_counter(ThreadpoolHttpServer::SOCKET_IPC);
  if (last_idle_counter < idle_counter_) {
    LOG(WARNING) << "not idle:" << last_idle_counter << " < " << idle_counter_;
    return;
  }
  StartGomaCtlPull();
}

void AutoUpdater::StartGomaCtlPull() {
  CHECK(server_ != nullptr);
  string goma_ctl = file::JoinPath(dir_, goma_ctl_);
  std::vector<const char*> args;
  args.push_back(goma_ctl.c_str());
  args.push_back("pull");
  args.push_back(nullptr);
  SubProcessTask* subproc = nullptr;
  {
    AUTOLOCK(lock, &mu_);
    if (subproc_ != nullptr)
      return;
    subproc_ = new SubProcessTask(
        "auto_updater", goma_ctl.c_str(), const_cast<char**>(&args[0]));
    subproc = subproc_;
  }
  SubProcessReq* req = subproc->mutable_req();
  req->set_cwd(dir_);
  req->set_stdout_filename(file::JoinPath(dir_, "goma_pull.out"));
  req->set_stderr_filename(file::JoinPath(dir_, "goma_pull.err"));
  for (size_t i = 0; i < env_.size(); ++i) {
    req->add_env(env_[i]);
  }
  req->set_weight(SubProcessReq::HEAVY_WEIGHT);
  req->set_priority(SubProcessReq::LOW_PRIORITY);
  subproc->Start(NewCallback(this, &AutoUpdater::GomaCtlPullDone));
}

void AutoUpdater::GomaCtlPullDone() {
  int status = -1;
  {
    AUTOLOCK(lock, &mu_);
    if (subproc_ == nullptr)
      return;
    status = subproc_->terminated().status();
    subproc_ = nullptr;
    cond_.Signal();
  }
  if (status != 0) {
    LOG(ERROR) << goma_ctl_ << " pull failed. exit=" << status;
    return;
  }
  if (!ReadManifest(file::JoinPath(dir_, "latest/MANIFEST"),
                    &pulled_version_)) {
    LOG(ERROR) << "failed to read latest/MANIFEST";
    return;
  }
  if (my_version_ == pulled_version_) {
    LOG(INFO) << "no update";
    return;
  }
  if (my_version_ > pulled_version_) {
    LOG(ERROR) << "Version downgrade? " << my_version_
               << "=>" << pulled_version_
               << " ignored";
    return;
  }
  if (server_ == nullptr) {
    LOG(ERROR) << "Auto updater already stopped.";
    return;
  }
  // Check if server is still idle.  If it processes some requests, postpone
  // updating.
  int last_idle_counter =
      server_->idle_counter(ThreadpoolHttpServer::SOCKET_IPC);
  if (last_idle_counter < idle_counter_) {
    LOG(WARNING) << "not idle:" << last_idle_counter << " < " << idle_counter_;
    return;
  }
  StartGomaCtlUpdate();
}

void AutoUpdater::StartGomaCtlUpdate() {
  CHECK(server_ != nullptr);
  LOG(INFO) << "Update version " << my_version_ << " to " << pulled_version_;
  string goma_ctl = file::JoinPath(dir_, goma_ctl_);
  std::vector<const char*> args;
  args.push_back(goma_ctl.c_str());
  args.push_back("update");
  args.push_back(nullptr);
  SubProcessTask* subproc = new SubProcessTask(
      "auto_updater", goma_ctl.c_str(), const_cast<char**>(&args[0]));
  SubProcessReq* req = subproc->mutable_req();
  req->set_cwd(dir_);
  req->set_stdout_filename(file::JoinPath(dir_, "goma_update.out"));
  req->set_stderr_filename(file::JoinPath(dir_, "goma_update.err"));
  for (size_t i = 0; i < env_.size(); ++i) {
    req->add_env(env_[i]);
  }
  req->set_weight(SubProcessReq::HEAVY_WEIGHT);
  req->set_priority(SubProcessReq::LOW_PRIORITY);
  req->set_detach(true);
  subproc->Start(nullptr);
  // "goma_ctl.py update" runs in detached mode.
  // subproc will be deleted in Start(), and never send feedback from
  // subprocess_controller_server.
  // In "goma_ctl.py update", it will stop the compiler_proxy, updates
  // new binaries, and restart compiler_proxy again.
}

}  // namespace devtools_goma
