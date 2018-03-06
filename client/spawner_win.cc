// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "spawner_win.h"

#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#include <algorithm>
#include <deque>
#include <memory>
#include <sstream>

#include <glog/logging.h>

#include "absl/strings/string_view.h"
#include "absl/strings/match.h"
#include "cmdline_parser.h"
#include "compiler_specific.h"
#include "file.h"
#include "file_dir.h"
#include "mypath.h"
#include "path.h"
#include "util.h"

namespace {

const DWORD kWaitTimeout = 10;
const DWORD kTerminateExitCode = 1;

string GetSubprocTempDirectory() {
  std::ostringstream oss;
  oss << "goma_temp" << "." << GetCurrentProcessId();
  return file::JoinPath(devtools_goma::GetGomaTmpDir(), oss.str());
}

bool IsEnvVar(absl::string_view env_line, absl::string_view env_prefix) {
  return absl::StartsWithIgnoreCase(env_line, env_prefix);
}

string EscapeCommandlineArg(const string& arg) {
  // TODO: More accurate escape.
  // https://msdn.microsoft.com/en-us/library/17w5ykft(v=vs.85).aspx
  if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == string::npos) {
    return arg;
  }

  string escaped_arg;

  // escaped_arg will be double quoted.
  bool quote_end = true;

  // construct escaped arg from back to check double quotation is preceded
  // by back slash.
  for (int i = static_cast<int>(arg.size()) - 1; i >= 0; --i) {
    if (arg[i] == '"') {
      escaped_arg += "\"\\";
      quote_end = true;
      continue;
    } else if (arg[i] == '\\' && quote_end) {
      escaped_arg += '\\';
    } else {
      quote_end = false;
    }
    escaped_arg += arg[i];
  }

  std::reverse(escaped_arg.begin(), escaped_arg.end());

  VLOG(1) << "arg: `" << arg << "` -> `" << '"' + escaped_arg + '"' << "`";

  return '"' + escaped_arg + '"';
}

// Iter should be an iterator of string containers.
template <typename Iter>
string PrepareCommandLine(const char* cwd, const char* prog,
                          Iter env_begin, Iter env_end,
                          Iter argv_begin, Iter argv_end) {
  // Check if we have PATH spec and/or PATHEXT spec.
  static const size_t kPathLength = 5;
  static const char* kPathStr = "PATH=";
  static const size_t kPathExtLength = 8;
  static const char* kPathExtStr = "PATHEXT=";

  string path_spec;
  string pathext_spec;
  for (Iter i = env_begin; i != env_end; ++i) {
    if (IsEnvVar(*i, kPathStr)) {
      path_spec = i->substr(kPathLength);
    }
    if (IsEnvVar(*i, kPathExtStr)) {
      pathext_spec = i->substr(kPathExtLength);
    }
  }

  // TODO: remove this when |prog| become full path.
  CHECK(!path_spec.empty()) << "PATH env. should be set.";
  CHECK(!pathext_spec.empty()) << "PATHEXT env. should be set.";
  string command_line;
  if (!devtools_goma::GetRealExecutablePath(
      nullptr, prog, cwd, path_spec, pathext_spec, &command_line,
      nullptr, nullptr)) {
    return string();
  }

  if (command_line[0] != '\"') {
    command_line = EscapeCommandlineArg(command_line);
  }
  for (Iter i = argv_begin; i != argv_end; ++i) {
    // argv[0] should be prog.
    if (i == argv_begin)
      continue;
    command_line.append(" ");
    command_line.append(EscapeCommandlineArg(*i));
  }

  return command_line;
}

// Iter should be an iterator of string containers.
template <typename Iter>
void PrepareEnvBlock(Iter begin, Iter end, std::vector<char>* env) {
  const size_t kMaxEnv = 32767;
  env->resize(kMaxEnv);  // max env size
  size_t index = 0;
  for (Iter i = begin; i != end; i++) {
    const string& e = *i;
    size_t len = e.size();
    strcpy_s(&((*env)[index]), kMaxEnv - index, e.c_str());
    index += len + 1;
    if (index >= kMaxEnv) {
      LOG(WARNING) << "env block exceeds capacity";
      index = kMaxEnv - 1;
      break;
    }
  }
  env->at(index) = 0;
}

string CreateJobName(DWORD pid, const string& command) {
  std::ostringstream ss;
  // Get <prog> from |command|.
  std::vector<string> args;
  devtools_goma::ParseWinCommandLineToArgv(command, &args);

  ss << "goma job:"
     << " pid=" << pid
     << " exe=" << file::Basename(args[0]);
  string job_name(ss.str());
  if (job_name.length() > MAX_PATH)
    job_name.erase(MAX_PATH);
  return job_name;
}

void SetProcessMemoryUsage(HANDLE child_handle, SIZE_T* mem_bytes) {
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(child_handle, &pmc, sizeof(pmc))) {
    *mem_bytes = pmc.PeakWorkingSetSize;
  } else {
    LOG_SYSRESULT(GetLastError());
  }
}

bool WaitThread(devtools_goma::ScopedFd* thread, DWORD timeout) {
  if (thread->valid()) {
    DWORD r = WaitForSingleObject(thread->handle(), timeout);
    switch (r) {
      case WAIT_ABANDONED:
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "Wait: join Thread error?"
                   << " thread=" << thread->handle();
        break;
      case WAIT_OBJECT_0:
        thread->reset(nullptr);
        break;
      case WAIT_TIMEOUT:
        VLOG(1) << "wait timeout=" << timeout;
        return false;
      default:
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "Unexpected return value for WaitForSingleObject."
                   << " r=" << r;
        break;
    }
  }
  return true;
}

}  // namespace

namespace devtools_goma {

static const DWORD kInvalidProcessStatus = 0xffffffff;

string* SpawnerWin::temp_dir_;

/* static */
void SpawnerWin::Setup() {
  if (temp_dir_ != nullptr) {
    delete temp_dir_;
  }
  temp_dir_ = new string(GetSubprocTempDirectory());
  RecursivelyDelete(*temp_dir_);
  CHECK(File::CreateDir(temp_dir_->c_str(), 0755)) << temp_dir_->c_str();
  LOG(INFO) << "Create temp dir: " << *temp_dir_;
}

/* static */
void SpawnerWin::TearDown() {
  if (temp_dir_ == nullptr) {
    return;
  }
  if (RecursivelyDelete(*temp_dir_)) {
    LOG(INFO) << "Remove temp dir: " << *temp_dir_;
  } else {
    LOG(ERROR) << "Remove temp dir failed?: " << *temp_dir_;
  }
  delete temp_dir_;
  temp_dir_ = nullptr;
}

SpawnerWin::SpawnerWin()
    : input_thread_(nullptr), input_thread_id_(0), stop_input_thread_(false),
      output_thread_(nullptr), output_thread_id_(0),
      stop_output_thread_(nullptr), process_status_(kInvalidProcessStatus),
      process_mem_bytes_(0), is_signaled_(false) {
}

SpawnerWin::~SpawnerWin() {
  CleanUp();
}

int SpawnerWin::Run(const string& cmd, const std::vector<string>& args,
                    const std::vector<string>& envs, const string& cwd) {
  DCHECK(!child_process_.valid());

  std::vector<string> environs;
  for (const auto& e : envs) {
    if (temp_dir_ != nullptr) {
      if (IsEnvVar(e, "TEMP=")) {
        environs.push_back("TEMP=" + *temp_dir_);
        continue;
      }
      if (IsEnvVar(e, "TMP=")) {
        environs.push_back("TMP=" + *temp_dir_);
        continue;
      }
    }
    environs.push_back(e);
  }

  // Having files to redirect or console output should be gathered.
  // And do not detach.
  bool need_redirect =
      (!(stdin_filename_.empty() &&
         stdout_filename_.empty() &&
         stderr_filename_.empty()) ||
       console_output_) && !detach_;
  if (need_redirect) {
    DCHECK(!console_output_ ||
           (stdout_filename_.empty() && stderr_filename_.empty()))
        << "You cannot use SetFileRedirection with SetConsoleOutputBuffer"
        << " console_output_=" << console_output_
        << " stdout_filename_=" << stdout_filename_
        << " stderr_filename_=" << stderr_filename_;

    const string command_line =
        PrepareCommandLine(cwd.c_str(), cmd.c_str(),
                           environs.cbegin(), environs.cend(),
                           args.begin(), args.end());
    if (command_line.empty()) {
      return Spawner::kInvalidPid;
    }
    std::vector<char> env;
    PrepareEnvBlock(environs.cbegin(), environs.cend(), &env);
    return RunRedirected(command_line, &env, cwd, stdout_filename_,
                         stdin_filename_);
  }
  PROCESS_INFORMATION pi;
  STARTUPINFOA si;

  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&si, sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);

  DWORD create_flag = 0;
  if (detach_) {
    create_flag |= DETACHED_PROCESS;
  }

  string command_line =
      PrepareCommandLine(cwd.c_str(), cmd.c_str(),
                         environs.cbegin(), environs.cend(),
                         args.begin(), args.end());
  if (command_line.empty()) {
    return Spawner::kInvalidPid;
  }
  VLOG(1) << "Run: command_line:" << command_line
          << " cwd:" << cwd;

  std::vector<char> envp;
  PrepareEnvBlock(environs.cbegin(), environs.cend(), &envp);
  // If environment is empty, use parent process's environment.
  LPVOID env_ptr = envp[0] ? &(envp[0]) : nullptr;
  const DWORD process_create_flag =
      create_flag | CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB;
  if (CreateProcessA(nullptr, &(command_line[0]), nullptr, nullptr, FALSE,
                     process_create_flag, env_ptr, cwd.c_str(), &si, &pi)) {
    child_process_.reset(pi.hProcess);
    job_name_ = CreateJobName(pi.dwProcessId, command_line);
    VLOG(1) << "Job name:" << job_name_;
    child_job_ = AssignProcessToNewJobObject(
        child_process_.handle(), job_name_);

    process_status_ = STILL_ACTIVE;
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
  } else {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "failed to CreateProcess job_name=" << job_name_;
  }
  VLOG(1) << "Run: pid=" << pi.dwProcessId;
  return pi.dwProcessId;
}

void SpawnerWin::UpdateProcessStatus(DWORD timeout) {
  DWORD res = WaitForSingleObject(child_process_.handle(), timeout);

  if (res == WAIT_TIMEOUT) {
    process_status_ = STILL_ACTIVE;
    return;
  }

  if (res == WAIT_FAILED) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to update child process status. job_name="
               << job_name_;
    process_status_ = kTerminateExitCode;
    return;
  }

  DCHECK_EQ(res, WAIT_OBJECT_0);

  if (!GetExitCodeProcess(child_process_.handle(), &process_status_)) {
    // TODO: come up with good way to handle this.
    // I expect it temporary error, and return false to make a SpawnerWin user
    // ignore this error.
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Process should be signaled but we cannot get exit code."
               << " job_name=" << job_name_;
    // Assume the process is dead, and set kTerminateExitCode.
    process_status_ = kTerminateExitCode;
  }
}

bool SpawnerWin::KillAndWait(DWORD timeout) {
  if (!is_signaled_) {
    if (input_thread_.valid()) {
      stop_input_thread_ = true;
    }
    if (child_job_.valid()) {
      if (!TerminateJobObject(child_job_.handle(), kTerminateExitCode))
        LOG_SYSRESULT(GetLastError());
    } else {
      if (!TerminateProcess(child_process_.handle(), kTerminateExitCode))
        LOG_SYSRESULT(GetLastError());
    }
    is_signaled_ = true;
  }

  std::vector<HANDLE> handles;
  if (child_job_.valid())
    handles.push_back(child_job_.handle());
  handles.push_back(child_process_.handle());
  // Wait the process is terminated.
  // Since WaitForSingleObject(child_job_.handle()) seems not wait termination
  // of |child_process_|, we need to wait it.
  VLOG(1) << "Wait: child timeout=" << timeout;
  DWORD ret = WaitForMultipleObjects(
      handles.size(), &(handles[0]), TRUE, timeout);
  if (ret == WAIT_TIMEOUT) {
    VLOG(1) << "wait timeout=" << timeout;
    return true;
  } else if (ret < WAIT_OBJECT_0 || ret > WAIT_OBJECT_0 + handles.size() - 1) {
    // Some handlers are abandoned or WAIT_FAILED.
    // See: http://msdn.microsoft.com/en-us/library/windows/desktop/ms687025(v=vs.85).aspx
    // TODO: come up with good way to handle this.
    // I expect it temporary error, and return false to make a SpawnerWin user
    // ignore this error.
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Wait: termiante error? Process seems not signaled."
               << " WaitForMultipleObjects returned:" << ret
               << " nCount=" << handles.size()
               << " timeout=" << timeout
               << " job_name=" << job_name_;
    return false;
  }
  UpdateProcessStatus(timeout);
  return process_status_ == STILL_ACTIVE;
}

bool SpawnerWin::FinalizeProcess(DWORD timeout) {
  VLOG(1) << "Wait: child_process finished " << process_status_;
  if (!WaitThread(&input_thread_, timeout)) {
    LOG(WARNING) << "input thread timed out=" << timeout
                 << " job_name=" << job_name_;
  }
  CHECK(child_process_.valid());
  SetProcessMemoryUsage(child_process_.handle(), &process_mem_bytes_);
  child_process_.reset(nullptr);
  if (!child_job_.Close()) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to release child job handle. "
               << "job_name=" << job_name_;
  }
  // OutputThread should terminate with failing to read from child_stdout and
  // child_stderr from child process, which should happen when child process
  // has been terminated.
  // If the OutputThread doesn't finish with some error, we'll trigger
  // stop_output_thread_ in CleanUp to terminate OutputThread.
  if (!WaitThread(&output_thread_, INFINITE)) {
    LOG(INFO) << "output thread timed out=" << timeout
              << " job_name=" << job_name_;
  }
  LOG_IF(ERROR, stdout_file_.valid())
      << "stdout_file is still valid. job_name=" << job_name_;
  LOG_IF(ERROR, stderr_file_.valid())
      << "stderr_file is still valid. job_name=" << job_name_;
  return true;
}

bool SpawnerWin::Kill() {
  return KillAndWait(kWaitTimeout);
}

bool SpawnerWin::Wait(Spawner::WaitPolicy wait_policy) {
  const DWORD timeout =
      (wait_policy==Spawner::WAIT_INFINITE) ? INFINITE : kWaitTimeout;
  const bool need_kill = (wait_policy==Spawner::NEED_KILL);

  // child_process_ is valid while subprocess is running.
  if (!child_process_.valid()) {
    VLOG(1) << "Wait: child_process already invalid";
    CHECK_NE(STILL_ACTIVE, process_status_);
    LOG_IF(ERROR, stdout_file_.valid())
        << "stdout_file is still valid. job_name=" << job_name_;
    LOG_IF(ERROR, stderr_file_.valid())
        << "stderr_file is still valid. job_name=" << job_name_;
    return false;
  }
  UpdateProcessStatus(timeout);
  if (process_status_ != STILL_ACTIVE) {
    // Process is not active.
    return !FinalizeProcess(timeout);
  }
  // Process is still active.
  if (!need_kill) {
    return true;
  }

  VLOG(1) << "Wait: need kill";
  bool running = KillAndWait(timeout);
  if (running) {
    return true;
  }
  return !FinalizeProcess(timeout);
}

// TODO: make stderr stored to the specified file.
int SpawnerWin::RunRedirected(const string& command_line,
                              std::vector<char>* env,
                              const string& cwd,
                              const string& out_file,
                              const string& in_file) {
  VLOG(1) << "RunRedirect: command_line:" << command_line
          << " cwd:" << cwd
          << " out_file:" << out_file
          << " in_file:" << in_file;
  CHECK_GT(command_line.length(), 0U);
  stop_output_thread_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
  PCHECK(stop_output_thread_.valid());

  PROCESS_INFORMATION pi;
  STARTUPINFOA si;

  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&si, sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);

  SECURITY_ATTRIBUTES sa;

  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = nullptr;
  sa.bInheritHandle = TRUE;

  ScopedFd stdout_read_tmp, stderr_read_tmp;  // parent stdout/err read handle
  ScopedFd stdout_write, stderr_write;  // child stdout/err write handle
  ScopedFd stdin_write_tmp;  // parent stdin write handle
  ScopedFd stdin_read;  // child stdin read handle

  // Create child stdout pipe
  if (!CreatePipe(stdout_read_tmp.ptr(), stdout_write.ptr(), &sa, 0)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to create pipe for stdout. "
               << " cmd: " << command_line
               << " cwd: " << cwd;
    return kInvalidPid;
  }

  switch (console_output_option_) {
    case STDOUT_ONLY:
      stderr_write.reset(ScopedFd::OpenNull());
      if (!stderr_write.valid()) {
        LOG(ERROR) << "Failed to open NUL."
                   << " cmd: " << command_line
                   << " cwd: " << cwd;
        return kInvalidPid;
      }
      break;
    case MERGE_STDOUT_STDERR:
      // TODO: During development, I found that stderr output are
      //                  not redirected to the pipe as stdout.  Both MSDN and
      //                  CodeProject examples redirect out/err to same file.
      //                  I'm not sure if that's a bug on Windows side or my
      //                  end.  Due to schedule, I'll just output both to same
      //                  file for now.
      if (!DuplicateHandle(GetCurrentProcess(), stdout_write.handle(),
                           GetCurrentProcess(), stderr_write.ptr(),
                           0, TRUE, DUPLICATE_SAME_ACCESS)) {
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "Failed to duplicate stderr handle."
                   << " cmd: " << command_line
                   << " cwd: " << cwd;
        return kInvalidPid;
      }
      break;
    default:
      LOG(ERROR) << "Unknown console_output_option is set:"
                 << console_output_option_;
      return kInvalidPid;
  }

  // Create child stdin pipe
  if (!CreatePipe(stdin_read.ptr(), stdin_write_tmp.ptr(), &sa, 0)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to create pipe for stdin. "
               << " cmd: " << command_line
               << " cwd: " << cwd;
    return kInvalidPid;
  }

  if (!DuplicateHandle(GetCurrentProcess(), stdout_read_tmp.handle(),
                       GetCurrentProcess(), child_stdout_.ptr(),
                       0, FALSE, DUPLICATE_SAME_ACCESS)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to duplicate stdout handle."
               << " cmd: " << command_line
               << " cwd: " << cwd;
    return kInvalidPid;
  }

  if (!DuplicateHandle(GetCurrentProcess(), stdin_write_tmp.handle(),
                       GetCurrentProcess(), child_stdin_.ptr(),
                       0, FALSE, DUPLICATE_SAME_ACCESS)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to duplicate stdin handle."
               << " cmd: " << command_line
               << " cwd: " << cwd;
    return kInvalidPid;
  }

  stdout_read_tmp.reset(nullptr);
  stderr_read_tmp.reset(nullptr);
  stdin_write_tmp.reset(nullptr);

  if (!out_file.empty()) {
    string file_path = file::JoinPathRespectAbsolute(cwd, out_file);
    stdout_file_.reset(CreateFileA(file_path.c_str(), GENERIC_WRITE,
                                   FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
  }

  si.hStdOutput = stdout_write.handle();
  si.hStdInput = stdin_read.handle();
  si.hStdError = stderr_write.handle();
  si.wShowWindow = SW_HIDE;
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

  // If environment is empty, use parent process's environment.
  LPVOID env_ptr = (*env)[0] ? &((*env)[0]) : nullptr;
  string cmd = command_line;
  // TODO: Code around here looks like Run().
  // Can we share some code?
  const DWORD process_create_flag =
      CREATE_NEW_CONSOLE | CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB;
  BOOL result = CreateProcessA(nullptr, &(cmd[0]), nullptr, nullptr, TRUE,
                               process_create_flag,
                               env_ptr, cwd.c_str(), &si, &pi);

  if (!result) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to create process."
               << " cmd: " << command_line
               << " cwd: " << cwd;
    return kInvalidPid;
  }
  // Child launched, close parent copy of pipe handles.
  stdout_write.reset(nullptr);
  stderr_write.reset(nullptr);
  stdin_read.reset(nullptr);

  process_status_ = STILL_ACTIVE;
  child_process_.reset(pi.hProcess);
  job_name_ = CreateJobName(pi.dwProcessId, command_line);
  VLOG(1) << "Job name:" << job_name_;
  child_job_ = AssignProcessToNewJobObject(child_process_.handle(), job_name_);

  output_thread_.reset(
      CreateThread(nullptr, 0, OutputThread, this, 0, &output_thread_id_));
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);

  if (!in_file.empty()) {
    input_file_ = in_file;
    input_thread_.reset(
        CreateThread(nullptr, 0, InputThread, this, 0, &input_thread_id_));
  }

  VLOG(1) << "Run: pid=" << pi.dwProcessId;
  return pi.dwProcessId;
}

// static
ScopedFd SpawnerWin::AssignProcessToNewJobObject(
    ScopedFd::FileDescriptor child_process, const string& job_name) {
  ScopedFd job_fd(CreateJobObjectA(nullptr, job_name.c_str()));
  if (!job_fd.handle()) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "failed to CreateJobObject"
               << " job_name=" << job_name;
    return ScopedFd();
  }

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    LOG(ERROR) << "Object already exist."
               << " job_name=" << job_name;
    return ScopedFd();
  }

  // We kill all processes associated with the job when the handle is closed.
  // To force it, we prevent child processes from breaking away the job.
  // Note that we need to use JOBOBJECT_EXTENDED_LIMIT_INFORMATION to set them.
  // See:
  // http://msdn.microsoft.com/en-us/library/windows/desktop/ms684161(v=vs.85).aspx#managing_job_objects
  // http://msdn.microsoft.com/en-us/library/windows/desktop/ms684147(v=vs.85).aspx
  // http://msdn.microsoft.com/en-us/library/windows/desktop/ms684925(v=vs.85).aspx
  // http://msdn.microsoft.com/en-us/library/windows/desktop/ms686216(v=vs.85).aspx
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
  if (!QueryInformationJobObject(job_fd.handle(),
                                 JobObjectExtendedLimitInformation,
                                 &info,
                                 sizeof(info),
                                 nullptr)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "failed to get job extended limit info"
               << " job name=" << job_name;
    return ScopedFd();
  }
  info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  info.BasicLimitInformation.LimitFlags &= ~JOB_OBJECT_LIMIT_BREAKAWAY_OK;
  info.BasicLimitInformation.LimitFlags &=
      ~JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK;
  if (!SetInformationJobObject(job_fd.handle(),
                               JobObjectExtendedLimitInformation,
                               &info, sizeof(info))) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "failed to set job extended limit info"
               << " job name=" << job_name;
    return ScopedFd();
  }

  if (!AssignProcessToJobObject(job_fd.handle(), child_process)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "failed to AssignProcessToJobObject"
               << " job_name=" << job_name;
    return ScopedFd();
  }

  return job_fd;
}

void SpawnerWin::CleanUp() {
  VLOG(1) << "CleanUp";
  if (input_thread_.valid()) {
    LOG(ERROR) << "input_thread still valid. job_name=" << job_name_;
    CHECK_NE(::GetCurrentThreadId(), input_thread_id_);
    stop_input_thread_ = true;
    WaitForSingleObject(input_thread_.handle(), INFINITE);
    input_thread_.reset(nullptr);
  }
  process_status_ = kInvalidProcessStatus;
  child_process_.reset(nullptr);
  if (!child_job_.Close()) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to release child job handle."
               << " job_name=" << job_name_;
  }
  if (output_thread_.valid()) {
    LOG(ERROR) << "output_thread still valid. job_name=" << job_name_;
    CHECK_NE(::GetCurrentThreadId(), output_thread_id_);
    DCHECK(stop_output_thread_.handle());
    SetEvent(stop_output_thread_.handle());
    VLOG(2) << "Join OutputThread";
    WaitForSingleObject(output_thread_.handle(), INFINITE);
    output_thread_.reset(nullptr);
  }
  stop_output_thread_.reset(nullptr);
  stdout_file_.reset(nullptr);
  stderr_file_.reset(nullptr);

  child_stdin_.reset(nullptr);
  child_stdout_.reset(nullptr);
  child_stderr_.reset(nullptr);
  output_thread_id_ = 0;
}

bool SpawnerWin::WriteToPipe() {
  const char* filepath = input_file_.c_str();
  VLOG(1) << "WriteToPipe from " << filepath;
  ScopedFd input(CreateFileA(filepath, GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_READONLY, nullptr));
  DWORD read, written;
  CHAR buf[4096];
  BOOL success = FALSE;

  for (;;) {
    if (stop_input_thread_)
      break;
    success = ReadFile(input.handle(), buf, 4096, &read, nullptr);
    // End of file under synchronous read operation.
    // See: http://msdn.microsoft.com/en-us/library/windows/desktop/aa365690(v=vs.85).aspx
    if (success && read == 0) {
      break;
    }
    if (!success) {
      DWORD error = GetLastError();
      LOG_SYSRESULT(error);
      LOG(ERROR) << "ReadFile failed:"
                 << " filepath=" << filepath
                 << " read=" << read
                 << " job_name=" << job_name_;
      return false;
    }

    if (stop_input_thread_)
      break;
    success = WriteFile(child_stdin_.handle(), buf, read, &written, nullptr);
    // Since this is an anonymous pipe, WriteFile blocks until |read| bytes has
    // been written.
    // See "Remarks" section:
    // http://msdn.microsoft.com/en-us/library/windows/desktop/aa365152(v=vs.85).aspx
    if (!success) {
      DWORD error = GetLastError();
      // When the child is killed, WriteFile would fail with ERROR_BROKEN_PIPE.
      if (stop_input_thread_ && error == ERROR_BROKEN_PIPE) {
        VLOG(1) << "broken pipe caused by process termination."
                << " filepath=" << filepath
                << " read=" << read
                << " written=" << written;
        return false;
      }
      LOG_SYSRESULT(error);
      LOG(ERROR) << "WriteFile failed:"
                 << " filepath=" << filepath
                 << " read=" << read
                 << " written=" << written
                 << " job_name=" << job_name_;
      return false;
    }
    if (read != written) {
      LOG(ERROR) << "Failed to WriteFile |read| length."
                 << " The execution result may strange."
                 << " filepath=" << filepath
                 << " read=" << read
                 << " written=" << written
                 << " success=" << success
                 << " job_name=" << job_name_;
      return false;
    }
    VLOG(2) << "WriteToPipe read=" << read << " written=" << written;
  }

  // close the pipe handle so the child process stops reading.
  if (child_stdin_.Close()) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "close stdin handler failed."
               << " job_name=" << job_name_;
    return false;
  }
  VLOG(1) << "WriteToPipe finished";
  return true;
}

bool SpawnerWin::Redirect() {
  bool stdout_open = false;
  bool stderr_open = false;
  VLOG(1) << "Redirect";
  if (child_stdout_.valid()) {
    VLOG(2) << "ReadFromStdout";
    stdout_open = ReadFromPipe(child_stdout_.handle(), stdout_file_.handle());
  }
  if (child_stderr_.valid()) {
    VLOG(2) << "ReadFromStderr";
    stderr_open = ReadFromPipe(child_stderr_.handle(), stderr_file_.handle());
  }
  return stdout_open || stderr_open;
}

bool SpawnerWin::ReadFromPipe(HANDLE pipe, HANDLE file) {
  DWORD avail = 0;
  if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr)) {
    DWORD err = GetLastError();
    if (err != ERROR_HANDLE_EOF && err != ERROR_BROKEN_PIPE) {
      LOG_SYSRESULT(err);
      LOG(ERROR) << "PeekNamedPipe error:" << err
                 << " job_name=" << job_name_;
    }
    return false;
  }
  if (avail) {
    VLOG(2) << "ReadFromPipe avail=" << avail;
    std::unique_ptr<char[]> buffer(new char[avail + 1]);
    memset(buffer.get(), 0, avail + 1);
    DWORD read = 0, written = 0;
    DWORD r = ReadFile(pipe, buffer.get(), avail, &read, nullptr);
    if (!r) {
      LOG_SYSRESULT(GetLastError());
      LOG(ERROR) << "ReadFile err avail=" << avail
                 << " job_name=" << job_name_;
      return false;
    } else if (read == 0) {
      // reached EOF, but avail > 0 ?
      LOG(ERROR) << "ReadFile read 0 avail=" << avail
                 << " job_name=" << job_name_;
      return false;
    }
    if (file != INVALID_HANDLE_VALUE && file != 0) {
      r = WriteFile(file, buffer.get(), read, &written, nullptr);
      if (!r) {
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "WriteFile err size=" << read << " written=" << written
                   << " job_name=" << job_name_;
        return false;
      }
      LOG_IF(ERROR, read != written)
          << "WriteFile size=" << read << " written=" << written
          << " job_name=" << job_name_;
    } else {
      VLOG(1) << "ignored to output to log file";
    }
    VLOG(2) << "ReadFromPipe read=" << read << " written=" << written;
    if (console_output_) {
      console_output_->append(buffer.get(), read);
    }
  }
  return true;
}

void SpawnerWin::Flush() {
  VLOG(1) << "Flush";
  stdout_file_.reset(nullptr);
  stderr_file_.reset(nullptr);
}

/* static */
DWORD WINAPI SpawnerWin::InputThread(LPVOID thread_params) {
  SpawnerWin* self = reinterpret_cast<SpawnerWin*>(thread_params);
  DCHECK(self);

  // TODO: handles WriteToPipe error.
  self->WriteToPipe();
  return 0;
}

/* static */
DWORD WINAPI SpawnerWin::OutputThread(LPVOID thread_params) {
  SpawnerWin* self = reinterpret_cast<SpawnerWin*>(thread_params);
  DCHECK(self);

  HANDLE stop = self->stop_output_thread_.handle();

  for (;;) {
    bool active = self->Redirect();
    if (!active) {
      VLOG(1) << "OutputThread: redirect closed";
      break;
    }

    VLOG(2) << "OutputThread: Wait";
    DWORD r = WaitForSingleObject(stop, kWaitTimeout);
    if (r == WAIT_TIMEOUT) {
      continue;
    }
    switch (r) {
      case WAIT_OBJECT_0:
        LOG(WARNING) << "OutputThread: Stop before child process ended "
                     << "job_name=" << self->job_name_;
        break;
      case WAIT_ABANDONED:
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "Wait: stop_output_thread error? "
                   << " job_name=" << self->job_name_;
        break;
      default:
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "Unexpected return value from WaitForSingleObject."
                   << " r=" << r
                   << " job_name=" << self->job_name_;
        break;
    }
    self->Redirect();
    break;
  }
  self->Flush();
  return 0;
}

}  // namespace devtools_goma
