// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SPAWNER_POSIX_H_
#define DEVTOOLS_GOMA_CLIENT_SPAWNER_POSIX_H_

#include <stdint.h>
#include <string>
#include <vector>

#include "scoped_fd.h"
#include "simple_timer.h"
#include "spawner.h"

using std::string;

namespace devtools_goma {

// A subclass of Spawner for POSIX.
// It spawns a process internally to capture child process' output.
class SpawnerPosix : public Spawner {
 public:
  SpawnerPosix();
  ~SpawnerPosix() override;

  int Run(const string& cmd, const std::vector<string>& args,
          const std::vector<string>& envs, const string& cwd) override;
  ProcessStatus Kill() override;
  ProcessStatus Wait(WaitPolicy wait_policy) override;
  bool IsChildRunning() const override;
  bool IsSignaled() const override { return is_signaled_; }
  void SetSignaled() override { is_signaled_ = true; }
  int ChildStatus() const override { return status_; }
  int64_t ChildMemKb() const override { return process_mem_kb_; }
  int ChildTermSignal() const override { return signal_; }
  int prog_pid() const { return prog_pid_; }
  int monitor_pid() const { return monitor_pid_; }

 private:
  // Process id monitoring spawned process.
  pid_t monitor_pid_;

  // Process id spawned by |cmd| in Run.
  pid_t prog_pid_;

  ScopedFd exit_fd_;
  bool is_signaled_;
  int sent_sig_;
  SimpleTimer sig_timer_;

  int status_;
  int64_t process_mem_kb_;
  int signal_;

  string console_out_file_;

  DISALLOW_COPY_AND_ASSIGN(SpawnerPosix);
};

typedef SpawnerPosix PlatformSpawner;

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SPAWNER_POSIX_H_
