// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "subprocess_controller_server.h"

#include <string.h>
#include <memory>
#include <set>
#include <utility>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <unistd.h>
#endif

#include "absl/strings/ascii.h"
#include "compiler_specific.h"
#include "fileflag.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "path.h"
#include "platform_thread.h"
#include "subprocess_impl.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()

#ifdef _WIN32
#include "spawner_win.h"
#endif

namespace {

static bool CanKillCommand(absl::string_view command,
                           const std::set<string>& dont_kill_commands) {
  string prog = string(file::Stem(command));
#ifdef _WIN32
  absl::AsciiStrToLower(&prog);
#endif
  return dont_kill_commands.find(prog) == dont_kill_commands.end();
}

}  // namespace

namespace devtools_goma {

#ifdef THREAD_SANITIZER
// When tsan is enabled, signal is swallowed by sanitizer and causing long wait
// in select. To improve time in that case, we use small wait time for select.
// TODO: remove this when below issue is fixed
//               https://github.com/google/sanitizers/issues/838
static const int kIdleIntervalMilliSec = 50;
#else
static const int kIdleIntervalMilliSec = 500;
#endif

#ifndef _WIN32
static const int kWaitIntervalMilliSec = 5;
#endif

#ifndef _WIN32
// siginfo is passed from signal handler to SubProcessControllerServer loop.
static int g_signal_fd;

void SigChldAction(int signo ALLOW_UNUSED,
                   siginfo_t* siginfo,
                   void* context ALLOW_UNUSED) {
  if (write(g_signal_fd, siginfo, sizeof(siginfo_t)) != sizeof(siginfo_t))
    abort();
}
#endif

SubProcessControllerServer::SubProcessControllerServer(
    int sock_fd,
    SubProcessController::Options options)
    : sock_fd_(sock_fd),
#ifndef _WIN32
      signal_fd_(-1),
#endif
      timeout_millisec_(kIdleIntervalMilliSec),
      options_(std::move(options)) {
  LOG(INFO) << "SubProcessControllerServer started fd=" << sock_fd
            << " " << options_.DebugString();
#ifdef _WIN32
  SpawnerWin::Setup();
#endif
}

SubProcessControllerServer::~SubProcessControllerServer() {
#ifdef _WIN32
  SpawnerWin::TearDown();
#endif
  LOG(INFO) << "SubProcessControllerServer deleted.";
}

void SubProcessControllerServer::Loop() {
  VLOG(1) << "Loop";
#ifndef _WIN32
  SetupSigchldHandler();
#endif
  DCHECK(sock_fd_.valid());
#ifndef _WIN32
  DCHECK(signal_fd_.valid());
#endif
  for (;;) {
    if (!sock_fd_.valid()) {
      VLOG(1) << "sock_fd closed";
      break;
    }
    fd_set read_fd;
    fd_set write_fd;
    FD_ZERO(&read_fd);
    FD_ZERO(&write_fd);
    MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
    FD_SET(sock_fd_.get(), &read_fd);
    MSVC_POP_WARNING();
    if (has_pending_write()) {
      MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
      FD_SET(sock_fd_.get(), &write_fd);
      MSVC_POP_WARNING();
    }
    int max_fd = std::max<int>(-1, sock_fd_.get());
#ifndef _WIN32
    FD_SET(signal_fd_.fd(), &read_fd);
    max_fd = std::max(max_fd, signal_fd_.fd());
#endif
    struct timeval tv;
    tv.tv_sec = timeout_millisec_ / 1000;
    tv.tv_usec = (timeout_millisec_ - (tv.tv_sec * 1000)) * 1000;
    int r = select(max_fd + 1, &read_fd, &write_fd, nullptr, &tv);
    if (r < 0) {
      if (errno == EINTR || errno == EAGAIN)
        continue;
      PLOG(FATAL) << "select";
    }
    VLOG(2) << "r=" << r
            << " sock_fd=" << FD_ISSET(sock_fd_.get(), &read_fd)
#ifndef _WIN32
            << " signal_fd=" << FD_ISSET(signal_fd_.fd(), &read_fd)
#endif
            << " t=" << tv.tv_sec << "," << tv.tv_usec;
    if (r == 0) {
      DoTimeout();
      continue;
    }
    if (FD_ISSET(sock_fd_.get(), &write_fd)) {
      DoWrite();
      if (!has_pending_write()) {
        FlushLogFiles();
      }
    }
    if (FD_ISSET(sock_fd_.get(), &read_fd)) {
      DoRead();
    }
#ifndef _WIN32
    if (FD_ISSET(signal_fd_.fd(), &read_fd)) {
      DoSignal();
    }
#endif
  }
  LOG(INFO) << "Terminating...";
  FlushLogFiles();
  for (const auto& iter : subprocs_) {
    SubProcessImpl* s = iter.second.get();
    if (s->req().detach()) {
      continue;
    }
    std::unique_ptr<SubProcessTerminated> terminated;
    s->Kill();
    // Wait for the running subprocess termination.
    // Because Wait() would emit log message and it would take some time to
    // terminate the subprocess, it will sleep for a while.
    // b/5370450
    while ((terminated = s->Wait(true)) == nullptr) {
      devtools_goma::PlatformThread::Sleep(10000);
    }
  }
  FlushLogFiles();
  subprocs_.clear();
}

void SubProcessControllerServer::Register(std::unique_ptr<SubProcessReq> req) {
  LOG(INFO) << "id=" << req->id() << " Register " << req->trace_id();
  bool dont_kill = false;
  if (options_.dont_kill_subprocess ||
      !CanKillCommand(req->prog(), options_.dont_kill_commands)) {
    dont_kill = true;
  }
  VLOG(1) << "id=" << req->id() << " Kill? " << req->trace_id()
          << " prog=" << req->prog()
          << " dont_kill=" << dont_kill;
  SubProcessImpl* s = new SubProcessImpl(*req, dont_kill);
  CHECK(subprocs_.insert(std::make_pair(req->id(), s)).second);
  TrySpawnSubProcess();
}

void SubProcessControllerServer::RequestRun(
    std::unique_ptr<SubProcessRun> run) {
  VLOG(1) << "id=" << run->id() << " Run";
  SubProcessImpl* s = LookupSubProcess(run->id());
  if (s == nullptr) {
    LOG(WARNING) << "id=" << run->id() << " request run unknown id "
                 << "(maybe already killed?)";
    return;
  }
  s->RaisePriority();
  TrySpawnSubProcess();
}

void SubProcessControllerServer::Kill(std::unique_ptr<SubProcessKill> kill) {
  VLOG(1) << "id=" << kill->id() << " Kill";
  SubProcessImpl* s = LookupSubProcess(kill->id());
  if (s == nullptr) {
    LOG(WARNING) << "id=" << kill->id() << " kill unknown id "
                 << "(maybe already killed?)";
    return;
  }
  if (!s->Kill()) {
    std::unique_ptr<SubProcessTerminated> terminated(s->Wait(false));
    if (terminated != nullptr) {
      Terminated(std::move(terminated));
      return;
    }
    ErrorTerminate(kill->id(), SubProcessTerminated::kFailedToKill);
  }
}

void SubProcessControllerServer::SetOption(
    std::unique_ptr<SubProcessSetOption> opt) {
  if (opt->has_max_subprocs() &&
      options_.max_subprocs != opt->max_subprocs()) {
    if (opt->max_subprocs() > 0) {
      options_.max_subprocs = opt->max_subprocs();
      LOG(INFO) << "option changed: max_subprocs="
                << opt->max_subprocs();
    } else {
      LOG(WARNING) << "option max_subprocs is not changed: "
                   << "max_subprocs should be positive. value="
                   << opt->max_subprocs();
    }
  }

  if (opt->has_max_subprocs_low_priority() &&
      options_.max_subprocs_low_priority != opt->max_subprocs_low_priority()) {
    if (opt->max_subprocs_low_priority() > 0) {
      options_.max_subprocs_low_priority = opt->max_subprocs_low_priority();
      LOG(INFO) << "option changed: max_subprocs_low_priority="
                << opt->max_subprocs_low_priority();
    } else {
      LOG(WARNING) << "option max_subprocs_low_priority is not changed: "
                   << "max_subprocs_low_priority should be positive. value="
                   << opt->max_subprocs_low_priority();
    }
  }

  if (opt->has_max_subprocs_heavy_weight() &&
      options_.max_subprocs_heavy_weight != opt->max_subprocs_heavy_weight()) {
    if (opt->max_subprocs_heavy_weight() > 0) {
      options_.max_subprocs_heavy_weight = opt->max_subprocs_heavy_weight();
      LOG(INFO) << "option changed: max_subprocs_heavy_weight="
                << opt->max_subprocs_heavy_weight();
    } else {
      LOG(WARNING) << "option max_subprocs_heavy_weight is not changed: "
                   << "max_subprocs_heavy_weight should be positive. value="
                   << opt->max_subprocs_heavy_weight();
    }
  }
}

void SubProcessControllerServer::Started(
    std::unique_ptr<SubProcessStarted> started) {
  LOG(INFO) << "id=" << started->id() << " Started pid=" << started->pid();
  SendNotify(SubProcessController::STARTED, *started);
}

void SubProcessControllerServer::Terminated(
    std::unique_ptr<SubProcessTerminated> terminated) {
  LOG_IF(INFO, terminated->status() != SubProcessTerminated::kInternalError)
      << "id=" << terminated->id() << " Terminated"
      << " status=" << terminated->status();

  subprocs_.erase(terminated->id());
  SendNotify(SubProcessController::TERMINATED, *terminated);

  TrySpawnSubProcess();
}

SubProcessImpl* SubProcessControllerServer::LookupSubProcess(int id) {
  auto found = subprocs_.find(id);
  if (found == subprocs_.end()) {
    // There is information gap between server and client.
    // The server can execute a subprocess and send SubProcessTerminated
    // any time. If it send SubProcessTerminated, the subprocess's id is
    // removed from subprocs_.
    // If SubProcessTerminated is in-flight, the client does not know it
    // removed from server's subprocs_, and it may send the request for the id.
    // If the client is not broken, REGISTER should come before anything else.
    // We MUST NOT think unknown id as error.
    LOG(INFO) << "id=" << id << " failed to LookupSubProcess "
              << "(maybe already killed?)";
    // In case subprocess_controller_client leaks id,
    // we will send ErrorTerminate.
    ErrorTerminate(id, SubProcessTerminated::kFailedToLookup);
    return nullptr;
  }
  return found->second.get();
}

void SubProcessControllerServer::TrySpawnSubProcess() {
  VLOG(1) << "TrySpawnSubProcess";

  int running = 0;
  int num_heavy_weight = 0;
  SubProcessImpl* candidate = nullptr;
  // Find next candidate from subprocs_.
  // Higher priority will be selected.
  // If the same priority exists, oldest one (smallest id number in the
  // priority) will be selected.  In other words, latter subproc with the
  // same priority in the list would not be executed before former subproc.
  // subproc weight is not checked to select next candidate.
  for (const auto& iter : subprocs_) {
    SubProcessImpl* s = iter.second.get();
    VLOG(2) << s->req().id() << " " << s->req().trace_id()
            << " " << SubProcessState::State_Name(s->state());
    if (s->state() == SubProcessState::PENDING &&
        s->req().priority() == SubProcessReq::HIGHEST_PRIORITY) {
      // hightest priority is used in SubProcessTask::ReadCommandOutput.
      DCHECK_EQ(SubProcessReq::LIGHT_WEIGHT, s->req().weight());
      candidate = s;
      break;
    }
    if (s->state() == SubProcessState::RUN) {
      ++running;
      if (running >= options_.max_subprocs) {
        VLOG(1) << "Too many subprocesses already running";
        return;
      }
      if (s->req().weight() == SubProcessReq::HEAVY_WEIGHT) {
        ++num_heavy_weight;
      }
    }
    if (s->state() != SubProcessState::PENDING)
      continue;
    if (candidate == nullptr) {
      candidate = s;
      continue;
    }
    if (candidate->req().priority() == SubProcessReq::LOW_PRIORITY &&
        s->req().priority() == SubProcessReq::HIGH_PRIORITY) {
      candidate = s;
    }
  }
  if (candidate == nullptr) {
    VLOG(2) << "no candidate";
    return;
  }

  VLOG(2) << "candiate:" << candidate->req().id()
          << " " << candidate->req().trace_id();
  // Once a candidate is selected, check max_subprocs_heavey_weight
  // and max_subprocs_low_priority.
  if (candidate->req().weight() == SubProcessReq::HEAVY_WEIGHT &&
      num_heavy_weight >= options_.max_subprocs_heavy_weight) {
    VLOG(1) << "Heavy weight subprocess already running "
            << num_heavy_weight;
    return;
  }

  if (candidate->req().priority() == SubProcessReq::LOW_PRIORITY &&
      running >= options_.max_subprocs_low_priority) {
    VLOG(1) << "candidate priority is low";
    return;
  }
  std::unique_ptr<SubProcessStarted> started(candidate->Spawn());
  if (started != nullptr) {
    Started(std::move(started));
    return;
  }
  if (candidate->req().detach()) {
    return;
  }
  ErrorTerminate(candidate->req().id(), SubProcessTerminated::kFailedToSpawn);
}

void SubProcessControllerServer::ErrorTerminate(
    int id, SubProcessTerminated_ErrorTerminate reason) {
  VLOG(1) << "id=" << id << " ErrorTerminate";
  std::unique_ptr<SubProcessTerminated> terminated(new SubProcessTerminated);
  terminated->set_id(id);
  terminated->set_status(SubProcessTerminated::kInternalError);
  terminated->set_error(reason);
  Terminated(std::move(terminated));
}

void SubProcessControllerServer::SendNotify(
    int op, const google::protobuf::Message& message) {
  VLOG(2) << "SendNotify op=" << op << " message=" << message.DebugString();
  AddMessage(op, message);
}

void SubProcessControllerServer::DoWrite() {
  VLOG(2) << "DoWrite";
  WriteMessage(&sock_fd_);
}

void SubProcessControllerServer::DoRead() {
  VLOG(2) << "DoRead";
  int op = 0;
  int len = 0;
  if (!ReadMessage(&sock_fd_, &op, &len)) {
    return;
  }
  VLOG(2) << "op=" << op << " len=" << len;
  switch (op) {
    case SubProcessController::CLOSED:
      sock_fd_.reset(-1);
      break;

    case SubProcessController::REGISTER: {
        std::unique_ptr<SubProcessReq> req(new SubProcessReq);
        if (req->ParseFromArray(payload_data(), len)) {
          Register(std::move(req));
        } else {
          LOG(ERROR) << "broken SubProcessReq";
        }
      }
      break;

    case SubProcessController::REQUEST_RUN: {
        std::unique_ptr<SubProcessRun> run(new SubProcessRun);
        if (run->ParseFromArray(payload_data(), len)) {
          RequestRun(std::move(run));
        } else {
          LOG(ERROR) << "broken SubProcessRun";
        }
      }
      break;

    case SubProcessController::KILL: {
        std::unique_ptr<SubProcessKill> kill(new SubProcessKill);
        if (kill->ParseFromArray(payload_data(), len)) {
          Kill(std::move(kill));
        } else {
          LOG(ERROR) << "broken SubProcessKill";
        }
      }
      break;

    case SubProcessController::SET_OPTION: {
        std::unique_ptr<SubProcessSetOption> option(new SubProcessSetOption);
        if (option->ParseFromArray(payload_data(), len)) {
          SetOption(std::move(option));
        } else {
          LOG(ERROR) << "broken SubProcessSetOption";
        }
      }
      break;
    default:
      LOG(FATAL) << "Unknown SubProcessController::Op " << op;
  }
  ReadDone();
  return;
}

#ifndef _WIN32
void SubProcessControllerServer::SetupSigchldHandler() {
  int fds[2];
  PCHECK(pipe(fds) == 0);
  signal_fd_.reset(fds[0]);
  g_signal_fd = fds[1];
  SetFileDescriptorFlag(g_signal_fd, FD_CLOEXEC);

  struct sigaction sa;
  memset(&sa, 0, sizeof(struct sigaction));
  sa.sa_sigaction = SigChldAction;
  sa.sa_flags = SA_NOCLDSTOP | SA_SIGINFO | SA_RESTART;
  PCHECK(sigaction(SIGCHLD, &sa, nullptr) == 0);
}

void SubProcessControllerServer::DoSignal() {
  VLOG(1) << "DoSignal";
  siginfo_t si;
  int r = read(signal_fd_.fd(), &si, sizeof(si));
  if (r <= 0) {
    PLOG(FATAL) << "signal_fd " << r;
  }
  LOG(INFO) << "signal pid=" << si.si_pid << " status=" << si.si_status;
  for (const auto& iter : subprocs_) {
    SubProcessImpl* s = iter.second.get();
    if (s->started().pid() == si.si_pid) {
      s->Signaled(si.si_status);
      timeout_millisec_ = kWaitIntervalMilliSec;
      return;
    }
  }
  LOG(WARNING) << "no subprocess found for pid:" << si.si_pid;
  timeout_millisec_ = kIdleIntervalMilliSec;
}
#endif

void SubProcessControllerServer::DoTimeout() {
  VLOG(1) << "DoTimeout";
  bool check_terminated = true;
  bool in_signaled = false;
  while (check_terminated) {
    check_terminated = false;
    in_signaled = false;
    for (const auto& iter : subprocs_) {
      SubProcessImpl* s = iter.second.get();
      if (s->started().pid() == SubProcessState::kInvalidPid)
        continue;
      bool need_kill = s->state() == SubProcessState::SIGNALED;
      if (need_kill)
        in_signaled = true;
      std::unique_ptr<SubProcessTerminated> terminated(s->Wait(need_kill));
      if (terminated != nullptr) {
        Terminated(std::move(terminated));
        // subprocs_ was modified, so iter was invalidated.
        check_terminated = true;
        break;
      }
    }
  }
  // If no subprocess is in SIGNALED, we don't need to wait for terminated
  // task in kWaitIntervalMilliSec.
  if (!in_signaled)
    timeout_millisec_ = kIdleIntervalMilliSec;
}

}  // namespace devtools_goma
