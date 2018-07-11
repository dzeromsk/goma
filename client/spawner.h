// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SPAWNER_H_
#define DEVTOOLS_GOMA_CLIENT_SPAWNER_H_

#include <stdint.h>
#include <string>
#include <vector>

#ifdef _WIN32
#include "config_win.h"
#endif

using std::string;

namespace devtools_goma {

// A subclass of Spawner spawns a child process.  It takes a file as stdin
// for the child process, and redirects the child process' stdout to another
// file.
//
// Spawning a process may cause strange behavior under multi-threaded
// environment especially in posix, and prohibited in our code base.
// You might only need to use a subclass of this class in:
// gomacc (not dispatcher) or subprocess_impl.
class Spawner {
 public:
  enum WaitPolicy {
    NO_HANG = 0,
    WAIT_INFINITE = 1,
    NEED_KILL = 2,
  };
  enum ConsoleOutputOption {
    MERGE_STDOUT_STDERR = 0,
    STDOUT_ONLY = 1,
  };

  enum class ProcessStatus {
    RUNNING,
    EXITED,
  };

  static const int kInvalidPid;

  virtual ~Spawner() {}

  // Set files for redirection.
  // If either of |stdin_filename|, |stdout_filename|, or |stderr_filename| is
  // not empty, it is used as stdin, stdout, or stderr of the child process.
  // |option| to specify which console outputs are stored to |stdout_filename|
  // when |stderr_filename| is empty.
  // Note: this must be called BEFORE the Run method.
  // Note: you cannot use this method with SetConsoleOutputBuffer.
  void SetFileRedirection(const string& stdin_filename,
                          const string& stdout_filename,
                          const string& stderr_filename,
                          ConsoleOutputOption option) {
    stdin_filename_ = stdin_filename;
    stdout_filename_ = stdout_filename;
    stderr_filename_ = stderr_filename;
    console_output_option_ = option;
  }

  // Set buffer to redirect stdout and stderr.
  // |option| to specify which console outputs are stored to |console_output|.
  // Note: this must be called BEFORE the Run method.
  // Note: if |stdout_filename| or |stderr_filename| are set by
  //       SetFileRedirection, you cannot use this method.
  void SetConsoleOutputBuffer(string* console_output,
                              ConsoleOutputOption option) {
    console_output_ = console_output;
    console_output_option_ = option;
  }

  // If |detach| is true, the Spawner detaches the process.
  // Note: this must be called BEFORE the Run method.
  void SetDetach(bool detach) { detach_ = detach; }

  // If |keep| is true, the Spawner does not override env
  // (e.g. TMP and/or TEMP are kept).
  void SetKeepEnv(bool keep) { keep_env_ = keep; }

  // If |umask| is positive value, it is used as umask of the process.
  // Note: this feature only works on SpawnerPosix.
  void SetUmask(int32_t umask) { umask_ = umask; }

  // Spawns a child process.
  // On Success,
  // * returns spawned process id in SpawnerWin.
  // * returns a monitor process id that spawned |cmd| in SpawnerPosix.
  // Note: SpawnerPosix::Run first spawn a monitor process, and then monitor
  // process spawn a child process not to inherit signal handlers.
  // Returns kInvalidPid on non fatal error, and dies with fatal error. |prog|
  // is a program name, |args| is its arguments, |envs| is its environment, and
  // |cwd| is a current working directory.
  virtual int Run(const string& cmd, const std::vector<string>& args,
                  const std::vector<string>& envs, const string& cwd) = 0;

  // Kills the process.
  virtual ProcessStatus Kill() = 0;

  // Waits for process termination.
  // If |wait_policy| is NO_HANG, it just returns current status.
  // If |wait_policy| is WAIT_INFINITE, it wait until the process finishes.
  // If |wait_policy| is NEED_KILL, it kills process if the process is running.
  virtual ProcessStatus Wait(WaitPolicy wait_policy) = 0;

  // Returns true if the process is running.
  virtual bool IsChildRunning() const = 0;

  // Returns true if the process is signaled.
  virtual bool IsSignaled() const = 0;

  // Set the process is signaled.
  virtual void SetSignaled() = 0;

  // Returns the exit code of the process.
  virtual int ChildStatus() const = 0;

  // Returns the memory used during the execution.
  // Returns -1 if this info is not available.
  virtual int64_t ChildMemKb() const = 0;

  // Returns the signal that caused the child process to terminate.
  // (Only meaningful for SpawnerPosix).
  virtual int ChildTermSignal() const = 0;

 protected:
  Spawner() :
      console_output_(NULL), detach_(false), keep_env_(false), umask_(-1),
      console_output_option_(MERGE_STDOUT_STDERR) {}

  string stdin_filename_, stdout_filename_, stderr_filename_;
  string* console_output_;
  bool detach_;
  bool keep_env_;
  int32_t umask_;
  ConsoleOutputOption console_output_option_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Spawner);
};

inline std::ostream& operator<<(std::ostream& os,
                                Spawner::ProcessStatus status) {
  switch (status) {
    case Spawner::ProcessStatus::RUNNING:
      return os << "RUNNING";
    case Spawner::ProcessStatus::EXITED:
      return os << "EXITED";
  }
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SPAWNER_H_
