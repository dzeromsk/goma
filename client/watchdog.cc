// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "watchdog.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#include "callback.h"
#include "compiler_specific.h"
#include "compile_service.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "mypath.h"
#include "path.h"
#include "threadpool_http_server.h"
#include "util.h"

#ifdef _WIN32
#include "posix_helper_win.h"
#endif

namespace devtools_goma {

#ifndef _WIN32
static const char *kGomaccName = "gomacc";
#else
static const char *kGomaccName = "gomacc.exe";
#endif

Watchdog::Watchdog()
    : dir_(GetMyDirectory()),
      gomacc_path_(file::JoinPath(dir_, kGomaccName)),
      server_(nullptr),
      idle_counter_(0),
      service_(nullptr),
      closure_id_(ThreadpoolHttpServer::kInvalidClosureId) {
}

Watchdog::~Watchdog() {
  LOG(INFO) << "stop watchdog";
  if (server_ && closure_id_ != ThreadpoolHttpServer::kInvalidClosureId) {
    server_->UnregisterIdleClosure(closure_id_);
    closure_id_ = ThreadpoolHttpServer::kInvalidClosureId;
  }
  server_ = nullptr;
  service_ = nullptr;
}

void Watchdog::Start(ThreadpoolHttpServer* server, int count) {
  LOG(INFO) << "start watchdog in " << count << " idle count.";
  server_ = server;
  idle_counter_ = count;
  std::unique_ptr<PermanentClosure> closure(
      NewPermanentCallback(this, &Watchdog::Check));
  closure_id_ = server_->RegisterIdleClosure(
      ThreadpoolHttpServer::SOCKET_IPC, count,
      std::move(closure));
}

void Watchdog::SetTarget(CompileService* service,
                         const std::vector<string>& goma_ipc_env) {
  service_ = service;
  goma_ipc_env_ = goma_ipc_env;
  LOG(INFO) << "watchdog target:" << goma_ipc_env;
}

void Watchdog::Check() {
  if (server_ == nullptr || service_ == nullptr) {
    LOG(ERROR) << "watchdog: no server or service.";
    return;
  }
  int last_idle_counter =
      server_->idle_counter(ThreadpoolHttpServer::SOCKET_IPC);
  if (last_idle_counter < idle_counter_) {
    LOG(WARNING) << "not idle:" << last_idle_counter << " < " << idle_counter_;
    return;
  }
  // Watchdog runs "gomacc port", which will call /portz, but we don't want
  // to make server as active by this request.
  // Keep idle while it's checking port via goma ipc.
  server_->SuspendIdleCounter();

  if (access(gomacc_path_.c_str(), X_OK) != 0) {
    LOG(INFO) << "gomacc:" << gomacc_path_ << " not found";
    service_->Quit();
    return;
  }

  std::vector<string> argv;
  argv.push_back(gomacc_path_);
  argv.push_back("port");
  std::vector<string> env(goma_ipc_env_);
  int32_t status = 0;
  const string out = ReadCommandOutput(gomacc_path_, argv, env, dir_,
                                       MERGE_STDOUT_STDERR, &status);
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput gets non-zero exit code. Going to quit."
               << " gomacc_path=" << gomacc_path_
               << " status=" << status
               << " cwd=" << dir_;
    service_->Quit();
    return;
  }
  int port = atoi(out.c_str());
  if (port != server_->port()) {
    LOG(INFO) << "gomacc port:" << port << " not match with"
              << " my port:" << server_->port()
              << " gomacc-out:" << out;
    service_->Quit();
    return;
  }
  LOG(INFO) << "gomacc port match with my port:" << port;
  server_->ResumeIdleCounter();
  FlushLogFiles();
}

}  // namespace devtools_goma
