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
#include "timestamp.h"
#include "mypath.h"
#include "path.h"

namespace {

static const int kInvalidProcessStatus = -256;

struct SubprocExit {
  SubprocExit() : lineno(0), last_errno(0), signal(0) {
    memset(&ru, 0, sizeof(ru));
  }
  int lineno;
  int last_errno;
  int signal;
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
    : pid_(Spawner::kInvalidPid),
      prog_pid_(Spawner::kInvalidPid),
      subprocess_dying_(false), is_signaled_(false),
      sent_sig_(0), status_(kInvalidProcessStatus), process_mem_kb_(-1),
      signal_(0) {
}

SpawnerPosix::~SpawnerPosix() {
  if (!console_out_file_.empty())
    remove(console_out_file_.c_str());
}

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
    pid_ = Spawner::kInvalidPid;
    return pid_;
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
      if (i == child_exit_fd.fd()) continue;
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
    } else {
      // Create own process group.
      if (setpgid(0, 0) != 0) {
        se.lineno = __LINE__ - 1;
        se.last_errno = errno;
        SubprocExitReport(child_exit_fd.fd(), se, 1);
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
    // Ignore SIGINT and SIGTERM handler and unblock them.
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &sa, nullptr) < 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
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
    if (write(child_exit_fd.fd(), &prog_pid, sizeof(prog_pid))
        != sizeof(prog_pid)) {
      se.lineno = __LINE__ - 2;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }

    int status = -1;
    while (waitpid(prog_pid, &status, 0) == -1) {
      if (errno != EINTR) break;
    }
    if (getrusage(RUSAGE_CHILDREN, &se.ru) != 0) {
      se.lineno = __LINE__ - 1;
      se.last_errno = errno;
      SubprocExitReport(child_exit_fd.fd(), se, 1);
    }
    if (WIFSIGNALED(status)) {
      se.signal = WTERMSIG(status);
    }
    SubprocExitReport(child_exit_fd.fd(), se, WEXITSTATUS(status));
  }
  pid_ = pid;
  int r = read(exit_fd_.fd(), &prog_pid_, sizeof(prog_pid_));
  if (r != sizeof(prog_pid_)) {
    PLOG(ERROR) << "failed to get prog_pid for pid=" << pid_;
    prog_pid_ = Spawner::kInvalidPid;
  }
  PCHECK(sigprocmask(SIG_UNBLOCK, &sigset, nullptr) == 0);

  return pid_;
}

bool SpawnerPosix::Kill() {
  int sig = SIGINT;
  if (pid_ == Spawner::kInvalidPid) {
    // means not started yet.
    return false;
  }
  if (!is_signaled_) {
    is_signaled_ = true;  // try to kill in Wait()
  } else {
    sig = SIGTERM;
  }
  bool running = (status_ == kInvalidProcessStatus);
  sent_sig_ = sig;
  sig_timer_.Start();
  if (prog_pid_ != Spawner::kInvalidPid) {
    if (kill(-prog_pid_, sig) != 0) {
      PLOG(WARNING) << " kill "
                    << " prog_pgrp=" << prog_pid_;
      if (kill(prog_pid_, sig) != 0) {
        PLOG(WARNING) << " kill "
                      << " prog_pid=" << prog_pid_;
      }
    }
  }
  if (kill(-pid_, sig) != 0) {
    PLOG(WARNING) << " kill "
                  << " pgrp=" << pid_;
    if (kill(pid_, sig) != 0) {
      PLOG(WARNING) << " kill "
                    << " pid=" << pid_;
      running = false;
    }
  }
  return running;
}

bool SpawnerPosix::Wait(WaitPolicy wait_policy) {
  int status = -1;
  if (pid_ != Spawner::kInvalidPid) {
    const bool need_kill = (wait_policy == NEED_KILL);
    const int waitpid_options = (wait_policy == WAIT_INFINITE) ? 0 : WNOHANG;
    int r;
    bool pgrp_dead = false;
    bool process_dead = false;
    if ((r = waitpid(-pid_, &status, waitpid_options)) == -1) {
      if (errno == ECHILD) {
        pgrp_dead = true;
      } else {
        PLOG(ERROR) << "waitpid " << " pgrp=" << pid_;
      }
    }
    if (r == -1) {
      // process might be killed before setting process group.
      if ((r = waitpid(pid_, &status, waitpid_options)) == -1) {
        if (errno == ECHILD) {
          process_dead = true;
        } else {
          PLOG(ERROR) << "waitpid " << " pid=" << pid_;
        }
      }
    }
    if (r == 0) {
      // one or more children in the process group exist, but have not yet
      // changed state.
      if (!need_kill) {
        // process is still running.
        DCHECK(!pgrp_dead || !process_dead);
        return false;
      }
    } else if (r == pid_) {
      status_ = WEXITSTATUS(status);
    }
    // Check subprocess itself and its process group still exist.
    // Note that subprocess didn't set own process group yet, so we need
    // check pid_ too.
    if (need_kill) {
      int sig = SIGINT;
      if (!pgrp_dead) {
        if (sent_sig_ == 0) {
          sent_sig_ = sig;
          sig_timer_.Start();
        }
        if (kill(-pid_, sig) == -1) {
          pgrp_dead = true;
        }
      }
      if (!process_dead) {
        if (sent_sig_ == 0) {
          sent_sig_ = sig;
          sig_timer_.Start();
        }
        if (kill(pid_, sig) == -1) {
          process_dead = true;
        }
      }
    }
    if (!pgrp_dead && kill(-pid_, 0) == -1) {
      pgrp_dead = true;
    }
    if (!process_dead && kill(pid_, 0) == -1) {
      process_dead = true;
    }
    if (pgrp_dead && process_dead) {
      if (subprocess_dying_) {
        LOG(INFO) << "all process were finished. pid=" << pid_;
      } else {
        VLOG(2) << "all processes were finished. pid=" << pid_;
      }
    } else {
      if (is_signaled_) {
        LOG_IF(INFO, !subprocess_dying_)
            << "process may still exist "
            << " need_kill=" << need_kill
            << " pgrp_dead=" << pgrp_dead
            << " process_dead=" << process_dead
            << " pid=" << pid_;
        subprocess_dying_ = true;
      } else {
        LOG_EVERY_N(INFO, 100)
            << "process is running "
            << " pid=" << pid_
            << " need_kill=" << need_kill
            << " pgrp_dead=" << pgrp_dead
            << " process_dead=" << process_dead
            << " is_signaled=" << is_signaled_;
      }
      return false;
    }
  }
  string sig_source;
  if (exit_fd_.valid()) {
    SubprocExit se;
    int r = read(exit_fd_.fd(), &se, sizeof(se));
    if (r == sizeof(se)) {
      if (se.lineno > 0 || se.last_errno > 0) {
        LOG(WARNING) << "subproc abort: pid=" << pid_
                     << " at " << __FILE__ << ":" << se.lineno
                     << " err=" << strerror(se.last_errno)
                     << "[" << se.last_errno << "]";
      }
      process_mem_kb_ = se.ru.ru_maxrss;
      signal_ = se.signal;
      sig_source = "subproc_exit";
      if (signal_ != 0 && signal_ != SIGINT && signal_ != SIGTERM) {
        LOG(WARNING) << "subproc was terminated unexpectedly."
                     << " pid=" << pid_
                     << " signal=" << signal_;
      } else if (signal_ == 0 && WIFSIGNALED(status)) {
        signal_ = WTERMSIG(status);
        sig_source = "wtermsig";
        if (signal_ != 0 && signal_ != SIGINT && signal_ != SIGTERM) {
          LOG(WARNING) << "mediator process was terminated unexpectedly."
                       << " pid=" << pid_
                       << " signal=" << signal_;
        }
      }
    } else {
      sig_source = "exit_fd_read_err";
      PLOG(WARNING) << "read SubprocExit:"
                    << " pid=" << pid_
                    << " ret=" << r;
    }
  } else {
    sig_source = "exit_fd_invalid";
  }
  if (console_output_) {
    DCHECK(!console_out_file_.empty());
    ReadFileToString(console_out_file_, console_output_);
  }
  LOG_IF(INFO, sent_sig_ != 0)
      << "signal=" << sent_sig_ << " sent to pid=" << pid_
      << " prog_pid=" << prog_pid_
      << " " << sig_timer_.GetInMs() << "msec ago,"
      << " terminated by signal=" << signal_
      << " from " << sig_source
      << " exit=" << status_;
  return true;
}

bool SpawnerPosix::IsChildRunning() const {
  return pid_ != Spawner::kInvalidPid && status_ == kInvalidProcessStatus;
}

}  // namespace devtools_goma
