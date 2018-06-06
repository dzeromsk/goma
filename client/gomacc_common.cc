// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "gomacc_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "compiler_flags.h"
#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "env_flags.h"
#include "file_helper.h"
#include "file_stat.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "goma_ipc_addr.h"
#include "gomacc_argv.h"
#include "ioutil.h"
#include "mypath.h"
#ifdef _WIN32
#include "named_pipe_client_win.h"
#endif
#include "path.h"  // file::JoinPath
#include "platform_thread.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "scoped_fd.h"
#include "simple_timer.h"
#include "socket_factory.h"
#include "subprocess.h"
#include "util.h"

#define GOMA_DECLARE_FLAGS_ONLY
#include "goma_flags.cc"

#ifdef _WIN32
#define isatty(x) false
#endif

namespace devtools_goma {

#ifdef _WIN32
// TODO: move it into goma_ipc, and use it in goma_ipc_unittest.cc?
class GomaIPCNamedPipeFactory : public GomaIPC::ChanFactory {
 public:
  GomaIPCNamedPipeFactory(const string& name, int timeout_ms)
      : factory_(name, timeout_ms) {
  }
  ~GomaIPCNamedPipeFactory() override {}

  GomaIPCNamedPipeFactory(const GomaIPCNamedPipeFactory&) = delete;
  GomaIPCNamedPipeFactory& operator=(const GomaIPCNamedPipeFactory&) = delete;

  std::unique_ptr<IOChannel> New() override {
    ScopedNamedPipe pipe = factory_.New();
    if (!pipe.valid()) {
      return nullptr;
    }
    return std::unique_ptr<IOChannel>(new ScopedNamedPipe(std::move(pipe)));
  }

  string DestName() const override {
    return factory_.DestName();
  }

 private:
  NamedPipeFactory factory_;
};

#else
class GomaIPCSocketFactory : public GomaIPC::ChanFactory {
 public:
  explicit GomaIPCSocketFactory(string socket_path)
      : socket_path_(std::move(socket_path)), addr_(nullptr), addr_len_(0) {
    addr_len_ = InitializeGomaIPCAddress(socket_path_, &un_addr_);
    addr_ = reinterpret_cast<const sockaddr*>(&un_addr_);
  }
  ~GomaIPCSocketFactory() override {
  }

  std::unique_ptr<IOChannel> New() override {
    ScopedSocket socket_fd(socket(AF_GOMA_IPC, SOCK_STREAM, 0));
    if (!socket_fd.valid())
      return nullptr;
    if (connect(socket_fd.get(), addr_, addr_len_) != -1) {
      if (!socket_fd.SetNonBlocking()) {
        LOG(ERROR) << "GOMA: failed to set nonblocking: " << socket_fd.get();
        return nullptr;
      }
      return std::unique_ptr<IOChannel>(new ScopedSocket(std::move(socket_fd)));
    }
    return nullptr;
  }

  string DestName() const override {
    return socket_path_;
  }

 private:
  const string socket_path_;
  GomaIPCAddr un_addr_;
  const sockaddr* addr_;
  socklen_t addr_len_;
  DISALLOW_COPY_AND_ASSIGN(GomaIPCSocketFactory);
};
#endif

int GetCompilerProxyPort(GomaIPC::Status* status) {
#ifndef _WIN32
  GomaIPC goma_ipc(std::unique_ptr<GomaIPC::ChanFactory>(
      new GomaIPCSocketFactory(
          file::JoinPathRespectAbsolute(GetGomaTmpDir(),
                                        FLAGS_COMPILER_PROXY_SOCKET_NAME))));
#else
  GomaIPC goma_ipc(std::unique_ptr<GomaIPC::ChanFactory>(
      new GomaIPCNamedPipeFactory(FLAGS_COMPILER_PROXY_SOCKET_NAME,
                                  FLAGS_NAMEDPIPE_WAIT_TIMEOUT_MS)));
#endif
  devtools_goma::EmptyMessage req;
  devtools_goma::HttpPortResponse resp;
  GomaIPC::Status status_buf;
  status_buf.health_check_on_timeout = false;
  if (status == nullptr) {
    status = &status_buf;
  }
  if (goma_ipc.Call("/portz", &req, &resp, status) < 0) {
    return -1;
  }
  return resp.port();
}

bool StartCompilerProxy() {
  if (!FLAGS_START_COMPILER_PROXY) {
#if defined(_WIN32)
    static const char kMsg[] =
        "compiler_proxy isn't running. Run 'goma_ctl.bat ensure_start'.";
#else
    static const char kMsg[] =
        "compiler_proxy isn't running. Run 'goma_ctl.py ensure_start'.";
#endif
    fputs(kMsg, stderr);
    exit(1);
  }

  if (FLAGS_COMPILER_PROXY_BINARY.empty()) {
    return false;
  }

  // Try to start up an instance of compiler proxy if it's not
  // already started.
  std::cerr << "GOMA: GOMA_START_COMPILER_PROXY=true."
            << " Starting compiler proxy" << std::endl;
#ifndef _WIN32
  devtools_goma::ScopedFd lock_fd(open(FLAGS_GOMACC_LOCK_FILENAME.c_str(),
                                       O_RDONLY|O_CREAT,
                                       0644));
  if (!lock_fd.valid()) {
    perror("open");
    std::cerr << "GOMA: Cannot open " << FLAGS_GOMACC_LOCK_FILENAME
              << std::endl;
    return false;
  }

  if (flock(lock_fd.fd(), LOCK_EX) == -1) {
    perror("flock failed");
    // Some weird error happened when trying to lock.
    return false;
  }
#else
  devtools_goma::ScopedFd lock_fd;
  lock_fd.reset(CreateEventA(nullptr, TRUE, FALSE,
                             FLAGS_GOMACC_LOCK_GLOBALNAME.c_str()));
  DWORD last_error = GetLastError();
  if (last_error == ERROR_ALREADY_EXISTS) {
    std::cerr << "GOMA: Someone already starting compiler proxy.";
    return false;
  }
  if (!lock_fd.valid()) {
    std::cerr << "GOMA: Cannot acquire global named object: " << last_error;
  }
#endif

  if (GetCompilerProxyPort(nullptr) >= 0) {
    if (FLAGS_DUMP) {
      std::cerr << "GOMA: Someone else already ran compiler proxy.";
    }
    return true;
  }

#ifndef _WIN32
  const string daemon_stderr = file::JoinPathRespectAbsolute(
      GetGomaTmpDir(), FLAGS_COMPILER_PROXY_DAEMON_STDERR);
  if (!FLAGS_COMPILER_PROXY_DAEMON_STDERR.empty() &&
      FLAGS_GOMACC_COMPILER_PROXY_RESTART_DELAY > 0) {
    struct stat st;
    if (stat(daemon_stderr.c_str(), &st) != -1) {
      struct timeval tv;
      PCHECK(gettimeofday(&tv, nullptr) == 0);
      if (st.st_size > 0 &&
          tv.tv_sec - st.st_mtime <
          FLAGS_GOMACC_COMPILER_PROXY_RESTART_DELAY) {
        // Don't retry starting proxy too soon if the last attempt seems
        // to have failed.
        return false;
      }
    }
  }
#endif

  const string& compiler_proxy_binary = file::JoinPath(
      GetMyDirectory(), FLAGS_COMPILER_PROXY_BINARY);

  if (FLAGS_DUMP) {
    std::cerr << "GOMA: " << "Invoke " << compiler_proxy_binary << std::endl;
  }

#ifndef _WIN32
  int pipe_fd[2];
  PCHECK(pipe(pipe_fd) == 0);

  pid_t pid;
  if (!(pid = fork())) {
    // child process, run compiler_proxy with default arguments.

    lock_fd.Close();
    close(pipe_fd[0]);

    std::set<int> preserve_fds;
    Daemonize(daemon_stderr, pipe_fd[1], preserve_fds);

    unsetenv("GOMA_COMPILER_PROXY_DAEMON_MODE");
    if (execlp(compiler_proxy_binary.c_str(),
                compiler_proxy_binary.c_str(),
                nullptr) == -1) {
      perror(("execlp compiler_proxy (" +
              compiler_proxy_binary +  ")").c_str());
    }
    exit(1);
  } else if (pid < 0) {
    // did not succeed in fork()
    perror("fork");
    std::cerr << "GOMA: fork failed." << std::endl;
    return false;
  }

  // Read out the proxy's actual pid.
  close(pipe_fd[1]);
  if (read(pipe_fd[0], &pid, sizeof(pid)) != sizeof(pid)) {
    char buf[1024];
    // Meaning of returned value of strerror_r is different between
    // XSI and GNU. Need to ignore.
    (void)strerror_r(errno, buf, sizeof buf);
    std::cerr << "GOMA: Could not get the proxy's pid.  Something went wrong:"
              << buf << std::endl;
    close(pipe_fd[0]);
    return false;
  }
  close(pipe_fd[0]);
#else
  PROCESS_INFORMATION pi;
  STARTUPINFOA si;

  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
  ZeroMemory(&si, sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);

  string path_env = GetEnv("PATH");
  CHECK(!path_env.empty()) << "No PATH env. found.";
  string command_path;
  // Note: "" to use the Windows default pathext.
  if (!GetRealExecutablePath(
      nullptr, "cmd.exe", "", path_env, "", &command_path, nullptr, nullptr)) {
    std::cerr << "GOMA: failed to find cmd.exe: "
              << " path_env=" << path_env
              << std::endl;
  }
  string command_line = command_path + " /k \"";
  command_line = command_line + compiler_proxy_binary;
  command_line = command_line + "\"";
  if (CreateProcessA(command_path.c_str(), &command_line[0], nullptr, nullptr,
                     FALSE, DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread);
  } else {
    DWORD error = GetLastError();
    std::cerr << "GOMA: failed to start compiler_proxy: " << error
              << std::endl;
  }
#endif

  int num_retries = 0;
  // Wait until compiler proxy becomes ready.
  while (GetCompilerProxyPort(nullptr) < 0) {
    // Make sure the proxy is running.
#ifndef _WIN32
    if (kill(pid, 0) == -1) {
      std::cerr << "GOMA: Failed to start compiler proxy." << std::endl;
      return false;
    }
#else
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    if (exit_code != STILL_ACTIVE) {
      std::cerr << "GOMA: compiler proxy died with exit code "
                << exit_code << std::endl;
      return false;
    }
#endif

    // If this loop takes more than 3 secs,
    num_retries++;
    if (num_retries++ >= 30 && num_retries % 10 == 0) {
      std::cerr << "GOMA: Compiler proxy is taking too much time to start. "
                << "Something might go wrong." << std::endl;
    }
    // Wait 100ms.
    PlatformThread::Sleep(100);
  }

  return true;
}

GomaClient::GomaClient(int id,
                       std::unique_ptr<CompilerFlags> flags,
                       const char** envp,
                       string local_compiler_path)
#ifndef _WIN32
    : goma_ipc_(std::unique_ptr<GomaIPC::ChanFactory>(new GomaIPCSocketFactory(
          file::JoinPathRespectAbsolute(GetGomaTmpDir(),
                                        FLAGS_COMPILER_PROXY_SOCKET_NAME)))),
#else
    : goma_ipc_(std::unique_ptr<GomaIPC::ChanFactory>(
          new GomaIPCNamedPipeFactory(FLAGS_COMPILER_PROXY_SOCKET_NAME,
                                      FLAGS_NAMEDPIPE_WAIT_TIMEOUT_MS))),
#endif
      id_(id),
      flags_(std::move(flags)),
      local_compiler_path_(std::move(local_compiler_path)) {
  flags_->GetClientImportantEnvs(envp, &envs_);

#ifdef _WIN32
  if (flags_->type() == CompilerType::Clexe) {
    for (const auto& file : flags_->optional_input_filenames()) {
      // Open the file while gomacc running to prevent from removal.
      optional_files_.push_back(new ScopedFd(ScopedFd::OpenForRead(file)));
    }
  }
#endif
  // used for logging.
  string buf;
  std::vector<string> info;
  info.push_back(flags_->compiler_name());
  if (flags_->type() == CompilerType::Gcc) {
    const GCCFlags& gcc_flags = static_cast<const GCCFlags&>(*flags_);
    switch (gcc_flags.mode()) {
      case GCCFlags::PREPROCESS:
        info.push_back("preprocessing");
        if (flags_->input_filenames().size() > 0)
          info.push_back(flags_->input_filenames()[0]);
        break;
      case GCCFlags::COMPILE:
        info.push_back("compiling");
        if (flags_->input_filenames().size() > 0)
          info.push_back(flags_->input_filenames()[0]);
        break;
      case GCCFlags::LINK:
        info.push_back("linking");
        if (flags_->output_files().size() > 0)
          info.push_back(flags_->output_files()[0]);
        break;
    }
  } else {
    if (flags_->input_filenames().size() > 0)
      info.push_back(flags_->input_filenames()[0]);
  }

  name_ = absl::StrJoin(info, " ");
}

GomaClient::~GomaClient() {
  if (stdin_file_.valid()) {
    remove(stdin_filename_.c_str());
  }
#ifdef _WIN32
  for (const auto& it : rsp_files_) {
    ScopedFd* fd = it.second;
    delete fd;
    DeleteFileA(it.first.c_str());
  }
  for (const auto* fd : optional_files_) {
    delete fd;
  }
#endif
}

void GomaClient::OutputResp() {
#ifdef _WIN32
  if (multi_exec_resp_.get()) {
    OutputMultiExecResp(multi_exec_resp_.get());
    return;
  }
#endif
  CHECK(exec_resp_.get() != nullptr);
  OutputExecResp(exec_resp_.get());
}

int GomaClient::retval() const {
#ifdef _WIN32
  if (multi_exec_resp_.get()) {
    for (const auto& it : multi_exec_resp_->response()) {
      if (it.resp().result().exit_status() != 0)
        return it.resp().result().exit_status();
    }
    return 0;
  }
#endif
  CHECK(exec_resp_.get());
  return exec_resp_->result().exit_status();
}

// Call IPC Request. Return IPC_OK if successful.
GomaClient::Result GomaClient::CallIPCAsync() {
  string request_path;
  std::unique_ptr<google::protobuf::Message> req;

#ifdef _WIN32
  if (FLAGS_FAN_OUT_EXEC_REQ && flags_->input_filenames().size() > 1) {
    std::unique_ptr<MultiExecReq> multi_exec_req(new MultiExecReq);
    request_path = "/me";
    PrepareMultiExecRequest(multi_exec_req.get());
    req = std::move(multi_exec_req);
    multi_exec_resp_.reset(new MultiExecResp);
  } else {
#endif
    std::unique_ptr<ExecReq> exec_req(new ExecReq);
    request_path = "/e";
    PrepareExecRequest(*flags_, exec_req.get());
    req = std::move(exec_req);
    exec_resp_ = absl::make_unique<ExecResp>();
#ifdef _WIN32
  }
#endif
  if (FLAGS_DUMP_REQUEST) {
    std::cerr << "GOMA:" << name_ << ": " << req->DebugString();
  }
  status_ = GomaIPC::Status();
  ipc_chan_ = goma_ipc_.CallAsync(request_path, req.get(), &status_);
  if (ipc_chan_ == nullptr) {
    if (status_.connect_success == true) {
      if (status_.http_return_code == 401) {
        std::cerr << "GOMA: Authentication failed (401)" << std::endl;
      } else if (status_.http_return_code == 400) {
        std::cerr << "GOMA: Bad request (400)" << std::endl;
      } else if (FLAGS_DUMP) {
        std::cerr << "GOMA: IPC Connection was successful but RPC failed"
                  << std::endl;
      }

      return IPC_REJECTED;
    }
    // If the failure reason was failure to connect, try starting
    // compiler proxy and retry the request.
    if (StartCompilerProxy()) {
      status_ = GomaIPC::Status();
      ipc_chan_ = goma_ipc_.CallAsync(request_path, req.get(), &status_);
      if (ipc_chan_ != nullptr) {
        // retry after starting compiler_proxy was successful
        if (FLAGS_DUMP) {
          std::cerr << "GOMA: Retry after starting compiler_proxy success"
                    << std::endl;
        }
      } else {
        // Even if we retried, we weren't successful, give up.
        if (FLAGS_DUMP) {
          std::cerr << "GOMA: Retry after starting compiler_proxy was "
                       "unsuccessful"
                    << std::endl;
        }
        return IPC_FAIL;
      }
    } else {
      // Starting compiler proxy was unsuccessful
      if (FLAGS_DUMP) {
        std::cerr << "GOMA: Could not connect to compiler_proxy and "
                     "starting it failed."
                  << std::endl;
      }
      return IPC_FAIL;
    }
  }
  return IPC_OK;
}

GomaClient::Result GomaClient::WaitIPC() {
  DCHECK(ipc_chan_ != nullptr);
  google::protobuf::Message* resp = nullptr;
#ifdef _WIN32
  if (multi_exec_resp_.get())
    resp = multi_exec_resp_.get();
#endif
  if (exec_resp_.get())
    resp = exec_resp_.get();

  if (goma_ipc_.Wait(std::move(ipc_chan_), resp, &status_) != OK)
    return IPC_FAIL;

  req_send_time_ = status_.req_send_time;
  resp_recv_time_ = status_.resp_recv_time;

  SimpleTimer timer;
  if (FLAGS_DUMP_RESPONSE) {
    std::cerr << "GOMA:" << name_ << ": " << resp->DebugString();
  }
  if (FLAGS_OUTPUT_EXEC_RESP) {
    OutputResp();
  }

  if (FLAGS_DUMP_TIME) {
    resp_write_time_ = timer.Get();
  }
  // TODO: check output files are written?


  if (FLAGS_DUMP_TIME) {
    std::cerr << "GOMA:" << name_
              << " send/recv/write="
              << req_send_time_ << "/"
              << resp_recv_time_ << "/"
              << resp_write_time_ << std::endl;
    // TODO: show more time metrics.
  }
  return IPC_OK;
}

string GomaClient::CreateStdinFile() {
#ifndef _WIN32
  stdin_filename_ = file::JoinPath(GetGomaTmpDir(), "gomacc.stdin.XXXXXX");
  stdin_file_.reset(mkstemp(&stdin_filename_[0]));
  for (;;) {
    char buf[4096];
    int r = read(STDIN_FILENO, buf, sizeof buf);
    if (r < 0) {
      if (errno == EINTR) continue;
      PLOG(ERROR) << "read";
      break;
    }
    if (r == 0) {
      break;
    }
    PCHECK(write(stdin_file_.fd(), buf, r) == r);
  }
#else
  char temp_file[MAX_PATH] = {0};
  GetTempFileNameA(GetGomaTmpDir().c_str(), "gomacc.stdin", 0, temp_file);
  stdin_filename_ = temp_file;
  stdin_file_.reset(ScopedFd::Create(stdin_filename_, 0600));
  char buf[4096];
  size_t actual_read = 0;
  while ((actual_read = fread(buf, 1, 4096, stdin)) > 0) {
    stdin_file_.Write(buf, actual_read);
  }
#endif
  return stdin_filename_;
}

GomaClient::Result GomaClient::CallIPC() {
  Result r = CallIPCAsync();
  if (r != IPC_OK)
    return r;
  return WaitIPC();
}

#ifdef _WIN32
bool GomaClient::PrepareMultiExecRequest(MultiExecReq* req) {
  const string tmpdir = devtools_goma::GetGomaTmpDir();
  pid_t pid = Getpid();

  std::set<string> input_filenames(flags_->input_filenames().begin(),
                              flags_->input_filenames().end());
  std::vector<string> args_no_input;  // args other than input filenames.
  // Input filenames may be in @rsp file, so scan expanded_args here.
  const std::vector<string>& expanded_args =
      (flags_->expanded_args().empty()
       ? flags_->args() : flags_->expanded_args());
  FanOutArgsByInput(expanded_args, input_filenames, &args_no_input);

  int nth = 0;
  for (std::set<string>::const_iterator iter = input_filenames.begin();
       iter != input_filenames.end();
       ++iter, ++nth) {
    const string& input_filename = *iter;
    const string cmdline = BuildArgsForInput(args_no_input, input_filename);
    std::stringstream fname;
    fname << file::Basename(input_filename)
          << "." << pid << "." << nth << ".rsp";
    const string rsp_filename = file::JoinPath(tmpdir, fname.str());
    if (!WriteStringToFile(cmdline, rsp_filename)) {
      LOG(ERROR) << "GOMA: Failed to create " << rsp_filename;
      return false;
    }
    // Keeps handle open, so that the rsp_file are not removed by tmp cleaner
    // while gomacc is running.
    rsp_files_.emplace_back(rsp_filename,
                            new ScopedFd(ScopedFd::OpenForRead(rsp_filename)));
    std::vector<string> args_of_input;
    args_of_input.push_back(flags_->args()[0]);
    args_of_input.push_back("@" + rsp_filename);
    std::unique_ptr<CompilerFlags> flags_of_input(
        CompilerFlags::MustNew(args_of_input, "."));
    if (!PrepareExecRequest(*flags_of_input, req->add_req())) {
      LOG(ERROR) << "GOMA: failed to create ExecReq for " << input_filename;
      return false;
    }
  }

  return true;
}

void GomaClient::OutputMultiExecResp(MultiExecResp* resp) {
  for (auto& exec_resp : *resp->mutable_response()) {
    OutputExecResp(exec_resp.mutable_resp());
  }
}
#endif

bool GomaClient::PrepareExecRequest(const CompilerFlags& flags, ExecReq* req) {
  req->mutable_command_spec()->set_name(
      flags.compiler_name());

  bool use_color_diagnostics = false;
#ifndef _WIN32
  if (GCCFlags::IsClangCommand(flags.compiler_name()) &&
      isatty(STDERR_FILENO)) {
    const char* term = getenv("TERM");
    if (term != nullptr && strcmp(term, "dump") != 0)
      use_color_diagnostics = true;
  }
#endif

  if (flags.type() == CompilerType::Gcc) {
    const GCCFlags& gcc_flags = static_cast<const GCCFlags&>(flags);
    if (gcc_flags.is_stdin_input()) {
      CHECK(!isatty(STDIN_FILENO)) << "goma doesn't support tty input."
                                   << flags.DebugString();
      ExecReq_Input* input = req->add_input();
      string tempfilename = CreateStdinFile();
      input->set_filename(tempfilename);
      input->set_hash_key("");
      DCHECK_EQ(req->input_size(), 1);
      FLAGS_RETRY = false;
    }
    if (FLAGS_FALLBACK_CONFTEST) {
      devtools_goma::RequesterEnv* requester_env = req->mutable_requester_env();
      for (const auto& input : gcc_flags.input_filenames()) {
        if (file::Stem(input) == "conftest") {
          FileStat file_stat(input);
          time_t now = time(nullptr);
          if (!file_stat.IsValid() || file_stat.mtime + 10 > now) {
            // probably conftest.c, force fallback.
            requester_env->add_fallback_input_file(input);
          }
        }
      }
    }
  }

  req->set_experimental_is_external_user(FLAGS_EXTERNAL_USER);

  // If local_compiler_path_ is empty, compiler proxy will find out
  // local compiler from requester_env's PATH and gomacc_path.
  if (gomacc_path_.empty()) {
    req->mutable_requester_env()->set_gomacc_path(GetMyPathname());
  } else {
    req->mutable_requester_env()->set_gomacc_path(gomacc_path_);
  }
  for (size_t i = 0; i < flags.args().size(); ++i) {
    req->add_arg(flags.args()[i]);
    if (i == 0 && use_color_diagnostics)
      req->add_arg("-fcolor-diagnostics");
  }
  if (cwd_.empty()) {
    cwd_ = GetCurrentDirNameOrDie();
  }
  req->set_cwd(cwd_);

  if (!local_compiler_path_.empty()) {
    req->mutable_command_spec()->set_local_compiler_path(local_compiler_path_);
  }

  req->mutable_requester_info()->set_api_version(
      RequesterInfo::CURRENT_VERSION);
  req->mutable_requester_info()->set_pid(Getpid());
  req->mutable_requester_info()->set_goma_revision(kBuiltRevisionString);

  if (FLAGS_STORE_ONLY) {
    if (FLAGS_USE_SUCCESS) {
      fprintf(stderr,
              "You cannot use both GOMA_STORE_ONLY and GOMA_USE_SUCCESS\n");
      exit(1);
    }
    req->set_cache_policy(ExecReq::STORE_ONLY);
  } else if (FLAGS_USE_SUCCESS) {
    req->set_cache_policy(ExecReq::LOOKUP_AND_STORE_SUCCESS);
  }

  for (size_t i = 0; i < envs_.size(); i++) {
    req->add_env(envs_[i]);
  }

  devtools_goma::RequesterEnv* requester_env = req->mutable_requester_env();
  const string path_env = GetEnv("PATH");
  if (!path_env.empty())
    requester_env->set_local_path(path_env);
  if (!FLAGS_VERIFY_COMMAND.empty()) {
    requester_env->set_verify_command(FLAGS_VERIFY_COMMAND);
    requester_env->set_use_local(false);
    requester_env->set_fallback(false);
  } else if (FLAGS_VERIFY_OUTPUT) {
    requester_env->set_verify_output(true);
    requester_env->set_use_local(true);
    requester_env->set_fallback(true);
  } else {
    if (FLAGS_USE_LOCAL)
      requester_env->set_use_local(true);
    if (FLAGS_FALLBACK)
      requester_env->set_fallback(true);
  }
  if (!FLAGS_FALLBACK_INPUT_FILES.empty()) {
    for (auto&& f : absl::StrSplit(FLAGS_FALLBACK_INPUT_FILES,
                                   ',',
                                   absl::SkipEmpty())) {
      requester_env->add_fallback_input_file(string(f));
    }
  }

  if (!FLAGS_IMPLICIT_INPUT_FILES.empty()) {
    // Set these file in ExecReq.
    // We don't need hash_key for these files here.
    // Compiler proxy picks them as required_files and computes hash_key.
    for (auto&& f : absl::StrSplit(FLAGS_IMPLICIT_INPUT_FILES,
                                   ',',
                                   absl::SkipEmpty())) {
      ExecReq_Input* input = req->add_input();
      input->set_filename(file::JoinPathRespectAbsolute(cwd_, f));
      input->set_hash_key("");
    }
  }
#ifndef _WIN32
  mode_t mask = umask(0000);
  umask(mask);
  requester_env->set_umask(mask);
#endif
  return true;
}

void GomaClient::OutputExecResp(ExecResp* resp) {
  WriteStdout(resp->result().stdout_buffer());
  WriteStderr(resp->result().stderr_buffer());
  for (int i = 0; i < resp->error_message_size(); i++) {
    std::cerr << "GOMA:" << name_
              << ":*ERROR*: " << resp->error_message(i) << std::endl;
  }
  resp->mutable_result()->clear_stdout_buffer();
  resp->mutable_result()->clear_stderr_buffer();
  resp->clear_error_message();
}

}  // namespace devtools_goma
