// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "spawner_posix.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <vector>
#include <sstream>

#include "file.h"
#include "file_helper.h"
#include "fileflag.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "mypath.h"
#include "path.h"
#include "platform_thread.h"
#include "timestamp.h"

namespace {

static const int kInvalidProcessStatus = -256;

struct SubprocExit {
  SubprocExit() : lineno(0), last_errno(0), status(kInvalidProcessStatus) {
    memset(&ru, 0, sizeof(ru));
  }
  int lineno;
  int last_errno;

  // status of spawned process.
  int status;
  struct rusage ru;
};

void __attribute__((__noreturn__)) SubprocExitReport(
    int fd, const SubprocExit& se, int exit_value) {
  if (write(fd, &se, sizeof(se)) != sizeof(se)) {
    close(fd);
    _exit(exit_value ? exit_value : 1);
  }
  _exit(exit_value);
}

}  // namespace

namespace devtools_goma {

SpawnerPosix::SpawnerPosix()
    : monitor_pid_(Spawner::kInvalidPid),
      prog_pid_(Spawner::kInvalidPid),
      is_signaled_(false),
      sent_sig_(0),
      status_(kInvalidProcessStatus),
      process_mem_kb_(-1),
      signal_(0) {}

SpawnerPosix::~SpawnerPosix() {
  if (!console_out_file_.empty())
    remove(console_out_file_.c_str());
}

const int Spawner::kInvalidPid = -1;

int SpawnerPosix::Run(const string& cmd, const std::vector<string>& args,
                      const std::vector<string>& envs, const string& cwd) {
  if (console_output_) {
    std::ostringstream filenamebuf;
    filenamebuf << "goma_tmp." << rand() << "." << GetCurrentTimestampMs()
                << ".out";
    console_out_file_ = file::JoinPath(GetGomaTmpDir(), filenamebuf.str());
    stdout_filename_ = console_out_file_;
  }

  const bool need_redirect =
      !(stdin_filename_.empty() &&
        stdout_filename_.empty() &&
        stderr_filename_.empty()) || detach_;
  ScopedFd stdin_fd;
  ScopedFd stdout_fd;
  ScopedFd stderr_fd;
  if (need_redirect) {
    ScopedFd devnullfd(ScopedFd::OpenNull());
    stdin_fd.reset(dup(devnullfd.fd()));
    if (!stdin_filename_.empty())
      stdin_fd.reset(ScopedFd::OpenForRead(stdin_filename_));
    stdout_fd.reset(dup(devnullfd.fd()));
    if (!stdout_filename_.empty()) {
      stdout_fd.reset(ScopedFd::Create(stdout_filename_, 0600));
    }
    stderr_fd.reset(dup(devnullfd.fd()));
    if (!stderr_filename_.empty()) {
      stderr_fd.reset(ScopedFd::Create(stderr_filename_, 0600));
    } else if (!stdout_filename_.empty() &&
               console_output_option_ == MERGE_STDOUT_STDERR) {
      // stdout is not empty, but stderr is empty.
      stderr_fd.reset(dup(stdout_fd.fd()));
    }
  }

  // Pipe for passing SubprocExit information.
  // pipe(7) says write(2) of less than PIPE_BUF bytes must be atomic.
  int pipe_fd[2];
  PCHECK(pipe(pipe_fd) == 0);
  exit_fd_.reset(pipe_fd[0]);
  ScopedFd child_exit_fd(pipe_fd[1]);
  // Make another pipe. Use this for report pid.
  PCHECK(pipe(pipe_fd) == 0);
  ScopedFd exit_pid_fd(pipe_fd[0]);
  ScopedFd child_pid_fd(pipe_fd[1]);

  // We can't use posix_spawn, because we'd like to control
  // current directory of each subprocess.
  const char* dir = cwd.c_str();
  const char* prog = cmd.c_str();
  std::vector<const char*> argvp;
  for (const auto& arg : args)
    argvp.push_back(arg.c_str());
  argvp.push_back(nullptr);
  std::vector<const char*> envp;
  for (const auto& env : envs)
    envp.push_back(env.c_str());
  envp.push_back(nullptr);

  // SubprocessImpl will try to send SIGINT or SIGTERM to kill the subprocess
  // but ignore them in this process. This process will wait for child process
  // termination (child process will be killed by SIGINT or SIGTERM to
  // the process group).
  // Also block SIGCHLD until it resets SIGCHLD in child process.
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);
  sigaddset(&sigset, SIGCHLD);
  PCHECK(sigprocmask(SIG_BLOCK, &sigset, nullptr) == 0);
  pid_t pid = fork();
  if (pid < 0) {
    PLOG(ERROR) << "fork failed. pid=" << pid;
    monitor_pid_ = Spawner::kInvalidPid;
    return Spawner::kInvalidPid;
  }
  if (pid == 0) {
    // child process.
    // You can use only async-signal safe functions here.
    //
    SubprocExit se;

    if (stdin_fd.valid() && dup2(stdin_fd.fd(), STDIN_FILENO) < 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    if (stdout_fd.valid() && dup2(stdout_fd.fd(), STDOUT_FILENO) < 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    if (stderr_fd.valid() && dup2(stderr_fd.fd(), STDERR_FILENO) < 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    for (int i = STDERR_FILENO + 1; i < 256; ++i) {
      if (i == child_exit_fd.fd() || i == child_pid_fd.fd()) {
        continue;
      }
      close(i);
    }

    if (detach_) {
      // Create own session.
      if (setsid() < 0) {
        se.lineno = __LINE__ -1;
        se.last_errno = errno;
        SubprocExitReport(child_exit_fd.fd(), se, 1);
      }
      pid_t pid;
      if ((pid = fork())) {
        if (pid < 0) {
          se.lineno = __LINE__ - 2;
          se.last_errno = errno;
          SubprocExitReport(child_exit_fd.fd(), se, 1);
        }
        exit(0);
      }
    }

    // Reset SIGCHLD handler.  we'll get exit status of prog_pid
    // by blocking waitpid() later.
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    if (sigaction(SIGCHLD, &sa, nullptr) < 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }

    sigset_t unblock_sigset;
    sigemptyset(&unblock_sigset);
    sigaddset(&unblock_sigset, SIGCHLD);
    sigaddset(&unblock_sigset, SIGINT);
    sigaddset(&unblock_sigset, SIGTERM);
    if (sigprocmask(SIG_UNBLOCK, &unblock_sigset, nullptr) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }

    if (chdir(dir) < 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }

    posix_spawnattr_t spawnattr;
    posix_spawnattr_init(&spawnattr);

    // Reset SIGINT and SIGTERM signal handlers in child process.
    if (posix_spawnattr_setflags(&spawnattr, POSIX_SPAWN_SETSIGDEF) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    sigset_t default_sigset;
    sigemptyset(&default_sigset);
    sigaddset(&default_sigset, SIGINT);
    sigaddset(&default_sigset, SIGTERM);
    if (posix_spawnattr_setsigdefault(&spawnattr, &default_sigset) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }

    // Don't mask any signals in child process.
    if (posix_spawnattr_setflags(&spawnattr, POSIX_SPAWN_SETSIGMASK) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    sigset_t sigmask;
    sigemptyset(&sigmask);
    if (posix_spawnattr_setsigmask(&spawnattr, &sigmask) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    if (umask_ >= 0) {
      umask(umask_);
    }

    // Let spawned process has its own pid/pgid.
    if (posix_spawnattr_setflags(&spawnattr, POSIX_SPAWN_SETPGROUP) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }

    pid_t prog_pid;
    // TODO: use POSIX_SPAWN_USEVFORK (_GNU_SOURCE).
    if (posix_spawn(
            &prog_pid, prog, nullptr, &spawnattr,
            const_cast<char**>(&argvp[0]), const_cast<char**>(&envp[0])) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    // report prog_pid to parent.
    if (write(child_pid_fd.fd(), &prog_pid, sizeof(prog_pid)) !=
        sizeof(prog_pid)) {
      se.lineno = __LINE__ - 2;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }

    while (waitpid(prog_pid, &se.status, 0) == -1) {
      if (errno != EINTR) break;
    }
    if (getrusage(RUSAGE_CHILDREN, &se.ru) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    int exit_status = -1;

    // The monitor process is considered as finishing successfully
    // (exit_status = 0) regardless of spawned process exit status.
    if (WIFSIGNALED(se.status) || WIFEXITED(se.status)) {
      exit_status = 0;
    }
    SubprocExitReport(child_exit_fd.fd(), se, exit_status);
  }

  // close writers (otherwise read(2) will be blocked)
  child_exit_fd.Close();
  child_pid_fd.Close();

  monitor_pid_ = pid;
  int r = read(exit_pid_fd.fd(), &prog_pid_, sizeof(prog_pid_));
  if (r != sizeof(prog_pid_)) {
    PLOG(ERROR) << "failed to get prog_pid for monitor_pid=" << monitor_pid_;
    prog_pid_ = Spawner::kInvalidPid;
  }
  PCHECK(sigprocmask(SIG_UNBLOCK, &sigset, nullptr) == 0);

  return monitor_pid_;
}

Spawner::ProcessStatus SpawnerPosix::Kill() {
  int sig = SIGINT;
  CHECK_NE(monitor_pid_, Spawner::kInvalidPid)
      << "Kill should not be called before the process has started.";

  if (!is_signaled_) {
    is_signaled_ = true;  // try to kill in Wait()
  } else {
    sig = SIGTERM;
  }

  ProcessStatus status = status_ == kInvalidProcessStatus
                             ? ProcessStatus::RUNNING
                             : ProcessStatus::EXITED;
  sent_sig_ = sig;
  sig_timer_.Start();

  if (status == ProcessStatus::RUNNING && prog_pid_ != Spawner::kInvalidPid) {
    if (kill(-prog_pid_, sig) != 0) {
      PLOG(WARNING) << " kill "
                    << " prog_pgrp=" << prog_pid_;
      if (kill(prog_pid_, sig) != 0) {
        PLOG(WARNING) << " kill "
                      << " prog_pid=" << prog_pid_;
      }
    }
  }
  return status;
}

SpawnerPosix::ProcessStatus SpawnerPosix::Wait(WaitPolicy wait_policy) {
  if (monitor_pid_ != Spawner::kInvalidPid) {
    const bool need_kill = (wait_policy == NEED_KILL);
    const int waitpid_options = (wait_policy == WAIT_INFINITE) ? 0 : WNOHANG;
    int r;
    int status = -1;
    while ((r = waitpid(monitor_pid_, &status, waitpid_options)) == -1) {
      if (errno == EINTR) {
        // Retry after 10 milliseconds wait.
        PlatformThread::Sleep(10);
        continue;
      }
      PLOG(FATAL) << "waitpid failed, monitor process id=" << monitor_pid_
                  << " waitpid_options=" << waitpid_options;
    }

    DCHECK_NE(r, -1);
    if (r == 0) {
      // monitor still running
      if (!need_kill) {
        CHECK_EQ(wait_policy, NO_HANG)
            << "process is alive in not NO_HANG policy."
            << " monitor_pid=" << monitor_pid_ << " prog_pid=" << prog_pid_;
        return ProcessStatus::RUNNING;
      }
      CHECK_EQ(wait_policy, NEED_KILL)
          << "try to kill process in other than NEED_KILL policy."
          << " monitor_pid=" << monitor_pid_ << " prog_pid=" << prog_pid_;

      CHECK_EQ(ProcessStatus::RUNNING, Kill())
          << "Should not call Kill when monitor process is not running.";

      while ((r = waitpid(monitor_pid_, &status, 0)) == -1) {
        if (errno == EINTR) {
          // Retry after 10 milliseconds wait.
          PlatformThread::Sleep(10);
          continue;
        }
        PLOG(FATAL) << "waitpid failed, monitor process id=" << monitor_pid_
                    << " waitpid_options=" << waitpid_options;
      }

      CHECK_EQ(r, monitor_pid_)
          << "unexpected waitpid returns, r=" << r << " status=" << status
          << " monitor_pid=" << monitor_pid_ << " prog_pid=" << prog_pid_;

      CHECK(WIFEXITED(status) || WIFSIGNALED(status))
          << "unexpected state change, r=" << r << " status=" << status
          << " monitor_pid=" << monitor_pid_ << " prog_pid=" << prog_pid_;
    } else if (r == monitor_pid_) {
      // monitor changed the status.
      CHECK(WIFEXITED(status))
          << "unexpected waitpid status:"
          << " status=" << status << " monitor_pid=" << monitor_pid_
          << " prog_pid=" << prog_pid_;

      if (WEXITSTATUS(status) != 0) {
        LOG(ERROR) << "monitor process died with non-zero exit status,"
                   << " exit_status=" << WEXITSTATUS(status)
                   << " status=" << status;
      }
    } else {
      LOG(FATAL) << "Unexpected waitpid is returned: r=" << r
                 << " status=" << status << " wait_policy=" << wait_policy
                 << " monitor_pid=" << monitor_pid_
                 << " prog_pid=" << prog_pid_;
    }
  }

  string sig_source;
  if (exit_fd_.valid()) {
    SubprocExit se;
    int r = read(exit_fd_.fd(), &se, sizeof(se));
    if (r == sizeof(se)) {
      if (se.lineno > 0 || se.last_errno > 0) {
        LOG(WARNING) << "subproc abort: monitor_pid=" << monitor_pid_ << " at "
                     << __FILE__ << ":" << se.lineno
                     << " err=" << strerror(se.last_errno) << "["
                     << se.last_errno << "]";
      }
      process_mem_kb_ = se.ru.ru_maxrss;
      if (se.status != kInvalidProcessStatus) {
        if (WIFSIGNALED(se.status)) {
          signal_ = WTERMSIG(se.status);
          sig_source = "wtermsig";
          status_ = 1;
        } else if (WIFEXITED(se.status)) {
          sig_source = "subproc_exit";
          status_ = WEXITSTATUS(se.status);
        } else {
          LOG(FATAL) << "Unexpected status from subproc."
                     << " monitor_pid=" << monitor_pid_
                     << " prog_pid=" << prog_pid_ << " status=" << se.status;
        }
      }

      if (signal_ != 0 && signal_ != sent_sig_) {
        LOG(ERROR) << "subproc was terminated unexpectedly."
                   << " monitor_pid=" << monitor_pid_
                   << " sent_sig=" << sent_sig_ << " prog_pid=" << prog_pid_
                   << " signal=" << signal_ << " status=" << se.status;
      }
    } else {
      sig_source = "exit_fd_read_err";
      PLOG(ERROR) << "read SubprocExit:"
                  << " monitor_pid=" << monitor_pid_ << " ret=" << r;
    }
  } else {
    sig_source = "exit_fd_invalid";
  }
  if (console_output_) {
    DCHECK(!console_out_file_.empty());
    ReadFileToString(console_out_file_, console_output_);
  }
  LOG_IF(INFO, sent_sig_ != 0)
      << "signal=" << sent_sig_ << " sent to monitor_pid=" << monitor_pid_
      << " prog_pid=" << prog_pid_ << " " << sig_timer_.GetInMs() << "msec ago,"
      << " terminated by signal=" << signal_ << " from " << sig_source
      << " exit=" << status_;
  monitor_pid_ = Spawner::kInvalidPid;
  return ProcessStatus::EXITED;
}

bool SpawnerPosix::IsChildRunning() const {
  return monitor_pid_ != Spawner::kInvalidPid &&
         status_ == kInvalidProcessStatus;
}

}  // namespace devtools_goma
