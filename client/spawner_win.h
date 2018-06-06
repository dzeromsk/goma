// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SPAWNER_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_SPAWNER_WIN_H_

#include "config_win.h"

#include <string>
#include <vector>

#include "basictypes.h"
#include "scoped_fd.h"
#include "spawner.h"

using std::string;

namespace devtools_goma {

// A subclass of Spawner for Windows.
// It spawns a thread internally to capture child process' output.
class SpawnerWin : public Spawner {
 public:
  SpawnerWin();
  ~SpawnerWin() override;

  int Run(const string& prog,
                  const std::vector<string>& argv,
                  const std::vector<string>& env,
                  const string& cwd) override;
  bool Kill() override;
  bool Wait(WaitPolicy wait_policy) override;
  bool IsChildRunning() const override {
    return process_status_ == STILL_ACTIVE;
  }
  bool IsSignaled() const override { return is_signaled_; }
  void SetSignaled() override { is_signaled_ = true; }
  int ChildStatus() const override {
    return static_cast<int>(process_status_);
  }
  int ChildTermSignal() const override {
    return 0;
  }
  // Not supported yet.
  int64_t ChildMemKb() const override {
    if (process_mem_bytes_ == 0)
      return -1;
    return static_cast<int64_t>(process_mem_bytes_) / 1024;
  }

  static void Setup();
  static void TearDown();

 private:
  int RunRedirected(const string& command_line,
                    std::vector<char>* env,
                    const string& cwd,
                    const string& out_file,
                    const string& in_file);
  void UpdateProcessStatus(DWORD timeout);
  // Returns true if the process is still active.
  bool KillAndWait(DWORD timeout);
  // Returns true when it has been finished.
  bool FinalizeProcess(DWORD timeout);

  // Returns true if it finish writing |input_file_| to |child_stdin_|.
  bool WriteToPipe();

  // Redirect stdout/stderr to file.
  // Returns true while still redirecting.
  // Returns false if both stdout/stderr pipes are closed.
  bool Redirect();
  // Returns true if pipe is still alive.
  bool ReadFromPipe(HANDLE pipe, HANDLE file);
  // Flush stdout/stderr files.
  void Flush();

  // CleanUp cleans up all threads and handles.
  void CleanUp();

  // Creates a new JobObject, and assign |child_process| to it.
  // Returns a ScopedFd for JobObject. When failed, invalid ScopedFd will be
  // returned.
  static ScopedFd AssignProcessToNewJobObject(
      ScopedFd::FileDescriptor child_process,
      const string& job_name);

  static unsigned __stdcall OutputThread(void* thread_params);
  static unsigned __stdcall InputThread(void* thread_params);

  ScopedFd input_thread_;  // thread to send input of the child process
  unsigned input_thread_id_;
  bool stop_input_thread_;  // Let InputThread to finish itself if this is true.

  ScopedFd output_thread_;  // thread to receive output of the child process
  unsigned output_thread_id_;
  ScopedFd stop_output_thread_;  // event to notify the redir thread to exit

  DWORD process_status_;
  SIZE_T process_mem_bytes_;

  string job_name_;
  ScopedFd child_job_;
  ScopedFd child_process_;
  ScopedFd child_stdin_, child_stdout_, child_stderr_;
  ScopedFd stdout_file_, stderr_file_;
  string input_file_;

  bool is_signaled_;

  static string* temp_dir_;

  DISALLOW_COPY_AND_ASSIGN(SpawnerWin);
};

typedef SpawnerWin PlatformSpawner;

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SPAWNER_WIN_H_
