// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "subprocess_controller.h"

#include <string.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <process.h>
#endif

#include <memory>
#include <string>
#include <sstream>

#include "breakpad.h"
#include "compiler_specific.h"
#include "compiler_proxy_info.h"
#include "env_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "google/protobuf/message.h"
MSVC_POP_WARNING()
#include "mypath.h"
#include "platform_thread.h"
#include "scoped_fd.h"
#include "subprocess_controller_client.h"
#include "subprocess_controller_server.h"

GOMA_DECLARE_bool(COMPILER_PROXY_ENABLE_CRASH_DUMP);

namespace devtools_goma {

const size_t SubProcessController::kMessageHeaderLen = sizeof(int) * 2;
const size_t SubProcessController::kOpOffset = 0;
const size_t SubProcessController::kSizeOffset = sizeof(int);

static const int kMaxSubProcs = 3;
static const int kMaxSubProcsForLowPriority = 1;
static const int kMaxSubProcsForHeavyWeight = 1;

SubProcessController::Options::Options()
    : max_subprocs(kMaxSubProcs),
      max_subprocs_low_priority(kMaxSubProcsForLowPriority),
      max_subprocs_heavy_weight(kMaxSubProcsForHeavyWeight),
      dont_kill_subprocess(false) {
}

string SubProcessController::Options::DebugString() const {
  std::ostringstream ss;
  ss << " max_subprocs=" << max_subprocs
     << " max_subprocs_low_priority=" << max_subprocs_low_priority
     << " max_subprocs_heavy_weight=" << max_subprocs_heavy_weight
     << " dont_kill_subprocess=" << dont_kill_subprocess;
  return ss.str();
}

/* static */
#ifndef _WIN32
void SubProcessController::Initialize(
    const char* arg0, const Options& options) {
  int sockfd[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockfd) != 0) {
    PLOG(FATAL) << "socketpair";
  }
  pid_t pid = fork();
  if (pid < 0) {
    PLOG(FATAL) << "fork";
  }
  if (pid == 0) {
    // child.
    string argv0(arg0);
    argv0 += "-subproc";

    ScopedFd devnullfd(ScopedFd::OpenNull());
    PCHECK(dup2(devnullfd.fd(), STDIN_FILENO) >= 0);
    PCHECK(dup2(devnullfd.fd(), STDOUT_FILENO) >= 0);
#ifndef KEEP_SUBPROC_STDERR
    PCHECK(dup2(devnullfd.fd(), STDERR_FILENO) >= 0);
#endif  // !KEEP_SUBPROC_STDERR
    devnullfd.reset(-1);
    close(sockfd[1]);
    for (int i = STDERR_FILENO + 1; i < 256; ++i) {
      if (i == sockfd[0]) continue;
      close(i);
    }

    google::InitGoogleLogging(argv0.c_str());
    google::InstallFailureSignalHandler();
    if (FLAGS_COMPILER_PROXY_ENABLE_CRASH_DUMP) {
      devtools_goma::InitCrashReporter(devtools_goma::GetCrashDumpDirectory());
    }
    LOG(INFO) << "goma built revision " << kBuiltRevisionString;
    {
      std::ostringstream ss;
      DumpEnvFlag(&ss);
      LOG(INFO) << "goma flags:" << ss.str();
    }
    LOG(INFO) << "SubProcessControllerServer launched";
    SubProcessControllerServer* server =
        new SubProcessControllerServer(sockfd[0], options);
    server->Loop();
    delete server;
    LOG(INFO) << "SubProcessControllerServer terminated";
    exit(0);
  }
  close(sockfd[0]);
  SubProcessControllerClient::Create(sockfd[1], pid, options);
}
#else

struct ServerParam {
  int sockfd_;
  SubProcessController::Options options_;
};

unsigned __stdcall SubProcessController::StartServer(void* param) {
  std::unique_ptr<ServerParam> args(reinterpret_cast<ServerParam*>(param));
  std::unique_ptr<SubProcessControllerServer> server(
      new SubProcessControllerServer(args->sockfd_, args->options_));
  server->Loop();
  LOG(INFO) << "SubProcessControllerServer terminated";
  return 0;
}

void SubProcessController::Initialize(
    const char*, const Options& options) {
  int sockfd[2];
  CHECK_EQ(async_socketpair(sockfd),  0);

  LOG(INFO) << "SubProcessControllerServer launching ...";
  ServerParam* args = new ServerParam;
  args->sockfd_ = sockfd[0];
  args->options_ = options;
  unsigned server_thread_id = 0;
  uintptr_t r =
      _beginthreadex(nullptr, 0, StartServer, args, 0, &server_thread_id);
  if (r == 0) {
    LOG(ERROR) << "failed to create thread for SubProcessController";
    return;
  }

  ScopedFd server_thread(reinterpret_cast<HANDLE>(r));
  SubProcessControllerClient::Create(sockfd[1], server_thread_id, options);
}
#endif

SubProcessController::SubProcessController()
    : read_len_(0) {
}

SubProcessController::~SubProcessController() {
}

bool SubProcessController::AddMessage(
    int op, const google::protobuf::Message& message) {
  int old_size = pending_write_.size();
  string msg;
  message.SerializeToString(&msg);
  int size = msg.size();
  pending_write_.resize(old_size + kMessageHeaderLen + size);
  memcpy(&pending_write_[old_size + kOpOffset], &op, sizeof(int));
  memcpy(&pending_write_[old_size + kSizeOffset], &size, sizeof(int));
  memcpy(&pending_write_[old_size + kMessageHeaderLen], msg.data(), size);
  return old_size == 0;
}

bool SubProcessController::has_pending_write() const {
  return !pending_write_.empty();
}

bool SubProcessController::WriteMessage(const IOChannel* fd) {
  VLOG(2) << "WriteMessage fd=" << *fd
          << " pending_write=" << pending_write_.size();
  if (pending_write_.empty())
    return false;

  int r = fd->Write(&pending_write_[0], pending_write_.size());
  if (r <= 0) {
    if (errno == EINTR || errno == EAGAIN)
      return true;
    PLOG(FATAL) << "write " << *fd << " failed " << r;
  }
  pending_write_ = pending_write_.substr(r);
  return !pending_write_.empty();
}

bool SubProcessController::ReadMessage(const IOChannel* fd,
                                       int* op, int* len) {
  VLOG(2) << "ReadMessage fd=" << *fd;
  if (pending_read_.empty()) {
    pending_read_.resize(kMessageHeaderLen);
    read_len_ = 0;
  }

  char* buf = &pending_read_[read_len_];
  int buf_size = pending_read_.size() - read_len_;
  int r = fd->Read(buf, buf_size);
  if (r == 0) {
    *op = CLOSED;
    return true;
  }
  if (r < 0) {
#ifndef _WIN32
    if (errno == EINTR || errno == EAGAIN)
      return false;
#endif
    PLOG(FATAL) << "read " << *fd << " failed " << r;
  }
  read_len_ += r;
  if (read_len_ >= kMessageHeaderLen) {
    const int* data = reinterpret_cast<int*>(&pending_read_[0]);
    *op = data[0];
    *len = data[1];
    if (kMessageHeaderLen + *len > pending_read_.size()) {
      pending_read_.resize(kMessageHeaderLen + *len);
      return false;
    }
    VLOG(2) << "ReadMessage op=" << *op
            << " len=" << *len
            << " read_len=" << read_len_;
    return (kMessageHeaderLen + *len) == read_len_;
  }
  return false;
}

const char* SubProcessController::payload_data() const {
  return &pending_read_[kMessageHeaderLen];
}

void SubProcessController::ReadDone() {
  VLOG(2) << "ReadDone";
  pending_read_.clear();
  read_len_ = 0;
}

}  // namespace devtools_goma
