// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Compiler proxy reimplemented as asynchronous.

#include <stdio.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __MACH__
#include <sys/sysctl.h>
#endif

#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "auto_updater.h"
#include "breakpad.h"
#include "compiler_info_cache.h"
#include "compiler_proxy_http_handler.h"
#include "counterz.h"
#include "cxx/include_processor/include_cache.h"
#include "cxx/include_processor/include_file_finder.h"
#include "deps_cache.h"
#include "glog/logging.h"
#include "goma_init.h"
#include "ioutil.h"
#include "list_dir_cache.h"
#include "local_output_cache.h"
#include "mypath.h"
#include "path.h"
#include "platform_thread.h"
#include "scoped_fd.h"
#include "settings.h"
#include "subprocess.h"
#include "subprocess_controller.h"
#include "subprocess_controller_client.h"
#include "subprocess_option_setter.h"
#include "subprocess_task.h"
#include "trustedipsmanager.h"
#include "util.h"
#include "watchdog.h"

using std::string;

#ifndef _WIN32
using devtools_goma::Daemonize;
#endif

namespace devtools_goma {

namespace {

#ifndef _WIN32
bool CheckFileOwnedByMyself(const string& filename, uid_t uid) {
  struct stat st;
  if (stat(filename.c_str(), &st) == -1)
    return true;
  if (st.st_uid == uid)
    return true;

  std::cerr << "GOMA: compiler_proxy:"
            << " other user (" << st.st_uid << ") owns " << filename
            << ", so you (" << uid << ") can not run compiler_proxy. "
            << std::endl;
  std::cerr << "GOMA: remove " << filename << std::endl;
  return false;
}

ScopedFd LockMyself(const string& filename, int port) {
  // Open myself and lock it during execution.
  std::ostringstream filename_buf;
  filename_buf << filename << "." << port;
  string lock_filename = filename_buf.str();
  if (!CheckFileOwnedByMyself(lock_filename.c_str(), getuid())) {
    exit(1);
  }
  ScopedFd fd(open(lock_filename.c_str(), O_RDONLY|O_CREAT, S_IRUSR));
  if (!fd.valid()) {
    std::cerr << "GOMA: compiler_proxy: "
              << "failed to open lock file:" << lock_filename << std::endl;
    exit(1);
  }
  int ret = flock(fd.fd(), LOCK_EX | LOCK_NB);
  if (ret == -1 && errno == EWOULDBLOCK) {
    std::cerr << "GOMA: compiler_proxy: "
              << "there is already someone else with lock" << std::endl;
    exit(1);
  }
  return fd;
}
#endif  // _WIN32

void InitResourceLimits(int* nfile) {
#ifndef _WIN32
  struct rlimit lim;
  PCHECK(getrlimit(RLIMIT_NOFILE, &lim) == 0);
  *nfile = static_cast<int>(lim.rlim_cur);
  const rlim_t prev = lim.rlim_cur;
  rlim_t open_max = static_cast<rlim_t>(sysconf(_SC_OPEN_MAX));
#ifdef OPEN_MAX
  open_max = std::max(open_max, static_cast<rlim_t>(OPEN_MAX));
#endif
  open_max = std::max(open_max, lim.rlim_cur);
#ifdef __MACH__
  // Choose smaller size from sysctl.  (b/9548636)
  int mib[2] = {CTL_KERN, -1};
  static const int kSecondMibs[] = {KERN_MAXFILES, KERN_MAXFILESPERPROC};
  for (const auto& it : kSecondMibs) {
    rlim_t tmp;
    size_t length = sizeof(tmp);
    mib[1] = it;
    PCHECK(sysctl(mib, 2, &tmp, &length, nullptr, 0) == 0) << it;
    open_max = std::min(tmp, open_max);
  }
  // setrlimit(3) will fail with EINVAL if launchctl sets smaller limit,
  // which default is 256.  b/11596636
#endif
  lim.rlim_cur = std::min(open_max, lim.rlim_max);
  if (setrlimit(RLIMIT_NOFILE, &lim) != 0) {
    // we might get EPERM or EINVAL if we try to increase RLIMIT_NOFILE above
    // the current kernel maxium.
    PLOG(ERROR) << "setrlimit(RLIMIT_NOFILE, &lim) != 0"
                << " rlim_cur:" << lim.rlim_cur
                << " rlim_max:" << lim.rlim_max
                << " rlim_cur would remain " << prev;
    lim.rlim_cur = prev;
  } else {
    LOG(INFO) << "setrlimit RLIMIT_NOFILE " << prev << " -> " << lim.rlim_cur;
  }
  *nfile = static_cast<int>(lim.rlim_cur);
#else
  *nfile = FLAGS_COMPILER_PROXY_MAX_SOCKETS;
#endif
}

void InitTrustedIps(TrustedIpsManager* trustedipsmanager) {
  for (auto&& ip : absl::StrSplit(FLAGS_COMPILER_PROXY_TRUSTED_IPS,
                                  ',',
                                  absl::SkipEmpty())) {
    trustedipsmanager->AddAllow(string(ip));
  }
}

void DepsCacheInit() {
  string cache_filename;
  if (!FLAGS_DEPS_CACHE_FILE.empty()) {
    cache_filename = file::JoinPathRespectAbsolute(GetCacheDirectory(),
                                                   FLAGS_DEPS_CACHE_FILE);
  }

  DepsCache::Init(
      cache_filename,
      FLAGS_DEPS_CACHE_IDENTIFIER_ALIVE_DURATION >= 0 ?
          absl::optional<absl::Duration>(
              absl::Seconds(FLAGS_DEPS_CACHE_IDENTIFIER_ALIVE_DURATION)) :
          absl::nullopt,
      FLAGS_DEPS_CACHE_TABLE_THRESHOLD,
      FLAGS_DEPS_CACHE_MAX_PROTO_SIZE_IN_MB);
}

void CompilerInfoCacheInit() {
  CompilerInfoCache::Init(
      GetCacheDirectory(), FLAGS_COMPILER_INFO_CACHE_FILE,
      absl::Seconds(FLAGS_COMPILER_INFO_CACHE_HOLDING_TIME_SEC));
}

}  // anonymous namespace

}  // namespace devtools_goma

int main(int argc, char* argv[], const char* envp[]) {
  devtools_goma::Init(argc, argv, envp);

#if HAVE_COUNTERZ
  if (FLAGS_ENABLE_COUNTERZ) {
    devtools_goma::Counterz::Init();
  }
#endif

  if (FLAGS_ENABLE_GLOBAL_FILE_STAT_CACHE ||
      FLAGS_ENABLE_GLOBAL_FILE_ID_CACHE) {
    devtools_goma::GlobalFileStatCache::Init();
  }

  const string tmpdir = FLAGS_TMP_DIR;
#ifndef _WIN32
  const string compiler_proxy_addr = file::JoinPathRespectAbsolute(
      tmpdir,
      FLAGS_COMPILER_PROXY_SOCKET_NAME);

  if (!devtools_goma::CheckFileOwnedByMyself(compiler_proxy_addr, getuid())) {
    exit(1);
  }

  const string lock_filename = file::JoinPathRespectAbsolute(
      tmpdir,
      FLAGS_COMPILER_PROXY_LOCK_FILENAME);
  devtools_goma::ScopedFd lockfd(
      devtools_goma::LockMyself(lock_filename, FLAGS_COMPILER_PROXY_PORT));
  if (FLAGS_COMPILER_PROXY_DAEMON_MODE) {
    int fd[2];
    PCHECK(pipe(fd) == 0);
    pid_t pid;
    if ((pid = fork())) {
      PCHECK(pid > 0);
      // Get pid from daemonized process
      close(fd[1]);
      pid_t server_pid;
      PCHECK(read(fd[0], &server_pid, sizeof(server_pid)) ==
             sizeof(server_pid));
      std::cout << server_pid << std::endl;
      exit(0);
    }
    close(fd[0]);
    std::set<int> preserve_fds;
    preserve_fds.insert(lockfd.fd());
    Daemonize(
        file::JoinPathRespectAbsolute(tmpdir,
                                      FLAGS_COMPILER_PROXY_DAEMON_STDERR),
        fd[1],
        preserve_fds);
  }

  // Initialize rand.
  srand(static_cast<unsigned int>(time(nullptr)));

  // Do not die with a SIGHUP and SIGPIPE.
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
#else
  // change directory to tmpdir, so that running process will keep
  // the directory and it makes it possible to remove the directory.
  LOG(INFO) << "chdir to " << tmpdir;
  if (!devtools_goma::Chdir(tmpdir.c_str())) {
    LOG(ERROR) << "failed to chdir to " << tmpdir;
  }
  const string compiler_proxy_addr = FLAGS_COMPILER_PROXY_SOCKET_NAME;
  WinsockHelper wsa;
  devtools_goma::ScopedFd lock_fd;
  std::ostringstream filename_buf;
  filename_buf << "Global\\" << FLAGS_COMPILER_PROXY_LOCK_FILENAME << "."
               << FLAGS_COMPILER_PROXY_PORT;
  string lock_filename = filename_buf.str();

  lock_fd.reset(CreateEventA(nullptr, TRUE, FALSE, lock_filename.c_str()));
  DWORD last_error = GetLastError();
  if (last_error == ERROR_ALREADY_EXISTS) {
    std::cerr << "GOMA: compiler proxy: already existed" << std::endl;
    exit(1);
  }

  // closes std handlers
  HANDLE devnull = devtools_goma::ScopedFd::OpenNull();
  CHECK_EQ(TRUE, SetStdHandle(STD_INPUT_HANDLE, devnull));
  CHECK_EQ(TRUE, SetStdHandle(STD_OUTPUT_HANDLE, devnull));
  CHECK_EQ(TRUE, SetStdHandle(STD_ERROR_HANDLE, devnull));

  if (!lock_fd.valid()) {
    LOG(ERROR) << "Cannot acquire global named object: " << last_error;
    exit(1);
  }

#ifdef NDEBUG
  // Sets error mode to SEM_FAILCRITICALERRORS and SEM_NOGPFAULTERRORBOX
  // to prevent from popping up message box on error.
  // We don't use CREATE_DEFAULT_ERROR_MODE for dwCreationFlags in
  // CreateProcess function.
  // http://msdn.microsoft.com/en-us/library/windows/desktop/ms680621(v=vs.85).aspx
  UINT old_error_mode = SetErrorMode(
      SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
  LOG(INFO) << "Set error mode from " << old_error_mode
            << " to " << GetErrorMode();
#endif
#endif
  devtools_goma::SubProcessController::Options subproc_options;
  subproc_options.max_subprocs = FLAGS_MAX_SUBPROCS;
  subproc_options.max_subprocs_low_priority = FLAGS_MAX_SUBPROCS_LOW;
  subproc_options.max_subprocs_heavy_weight = FLAGS_MAX_SUBPROCS_HEAVY;
  subproc_options.dont_kill_subprocess = FLAGS_DONT_KILL_SUBPROCESS;
  if (!FLAGS_DONT_KILL_COMMANDS.empty()) {
    for (auto&& cmd_view : absl::StrSplit(FLAGS_DONT_KILL_COMMANDS,
                                          ',',
                                          absl::SkipEmpty())) {
      string cmd(cmd_view);
#ifdef _WIN32
      absl::AsciiStrToLower(&cmd);
#endif
      subproc_options.dont_kill_commands.insert(cmd);
    }
  }
  devtools_goma::SubProcessController::Initialize(argv[0], subproc_options);

  devtools_goma::InitLogging(argv[0]);
  if (FLAGS_COMPILER_PROXY_ENABLE_CRASH_DUMP) {
    devtools_goma::InitCrashReporter(devtools_goma::GetCrashDumpDirectory());
    LOG(INFO) << "breakpad is enabled";
  }

  std::unique_ptr<devtools_goma::AutoUpdater> auto_updater;
  if (FLAGS_ENABLE_AUTO_UPDATE) {
    auto_updater =
        absl::make_unique<devtools_goma::AutoUpdater>(FLAGS_CTL_SCRIPT_NAME);
    if (auto_updater->my_version() > 0) {
      LOG(INFO) << "goma version:" << auto_updater->my_version();
    }
    auto_updater->SetEnv(envp);
  } else {
    LOG(INFO) << "auto updater is disabled";
  }

  int max_nfile = 0;
  devtools_goma::InitResourceLimits(&max_nfile);
  CHECK_GT(max_nfile, 0);
  int max_num_sockets = max_nfile;
  LOG(INFO) << "max_num_sockets=" << max_num_sockets
            << " max_nfile=" << max_nfile;

  devtools_goma::WorkerThreadManager wm;
  wm.Start(FLAGS_COMPILER_PROXY_THREADS);

  devtools_goma::SubProcessControllerClient::Initialize(&wm, tmpdir);

  devtools_goma::InstallReadCommandOutputFunc(
      &devtools_goma::SubProcessTask::ReadCommandOutput);

  devtools_goma::IncludeFileFinder::Init(FLAGS_ENABLE_GCH_HACK);

  devtools_goma::IncludeCache::Init(FLAGS_MAX_INCLUDE_CACHE_ENTRIES,
                                    !FLAGS_DEPS_CACHE_FILE.empty());
  devtools_goma::ListDirCache::Init(FLAGS_MAX_LIST_DIR_CACHE_ENTRY_NUM);

  std::unique_ptr<devtools_goma::WorkerThreadRunner> init_deps_cache(
      new devtools_goma::WorkerThreadRunner(
          &wm, FROM_HERE,
          devtools_goma::NewCallback(devtools_goma::DepsCacheInit)));
  std::unique_ptr<devtools_goma::WorkerThreadRunner> init_compiler_info_cache(
      new devtools_goma::WorkerThreadRunner(
          &wm, FROM_HERE,
          devtools_goma::NewCallback(devtools_goma::CompilerInfoCacheInit)));

  devtools_goma::TrustedIpsManager trustedipsmanager;
  devtools_goma::InitTrustedIps(&trustedipsmanager);

  string setting;
  if (!FLAGS_SETTINGS_SERVER.empty()) {
    setting = ApplySettings(FLAGS_SETTINGS_SERVER, FLAGS_ASSERT_SETTINGS, &wm);
  }
  std::unique_ptr<devtools_goma::CompilerProxyHttpHandler> handler(
      new devtools_goma::CompilerProxyHttpHandler(
          string(file::Basename(argv[0])), setting, tmpdir, &wm));

  devtools_goma::ThreadpoolHttpServer server(
      FLAGS_COMPILER_PROXY_LISTEN_ADDR, FLAGS_COMPILER_PROXY_PORT,
      FLAGS_COMPILER_PROXY_NUM_FIND_PORTS, &wm,
      FLAGS_COMPILER_PROXY_HTTP_THREADS, handler.get(), max_num_sockets);
  server.SetMonitor(handler.get());
  server.SetTrustedIpsManager(&trustedipsmanager);
  CHECK(!compiler_proxy_addr.empty())
      << "broken compiler_proxy_addr configuration. "
      << "set GOMA_COMPILER_PROXY_SOCKET_NAME"
      << " for compiler_proxy ipc addr";
  server.StartIPC(compiler_proxy_addr,
                  FLAGS_COMPILER_PROXY_THREADS,
                  FLAGS_MAX_OVERCOMMIT_INCOMING_SOCKETS);
  LOG(INFO) << "Started IPC server: " << compiler_proxy_addr;
  // TCP serves only status pages, no limit.
  if (auto_updater) {
    auto_updater->Start(&server, FLAGS_AUTO_UPDATE_IDLE_COUNT);
    handler->SetAutoUpdater(std::move(auto_updater));
  }
  if (FLAGS_WATCHDOG_TIMER > 0) {
    std::unique_ptr<devtools_goma::Watchdog> watchdog(
        new devtools_goma::Watchdog);
    std::vector<string> env;
    env.push_back("GOMA_COMPILER_PROXY_SOCKET_NAME=" + compiler_proxy_addr);
    env.push_back("PATH=" + devtools_goma::GetEnv("PATH"));
    env.push_back("PATHEXT=" + devtools_goma::GetEnv("PATHEXT"));
    env.push_back("USER=" + devtools_goma::GetUsername());
    env.push_back("GOMA_TMP_DIR=" + FLAGS_TMP_DIR);
    handler->SetWatchdog(std::move(watchdog), env,
                         &server, FLAGS_WATCHDOG_TIMER);
  }

  devtools_goma::LocalOutputCache::Init(
      FLAGS_LOCAL_OUTPUT_CACHE_DIR,
      &wm,
      FLAGS_LOCAL_OUTPUT_CACHE_MAX_CACHE_AMOUNT_IN_MB,
      FLAGS_LOCAL_OUTPUT_CACHE_THRESHOLD_CACHE_AMOUNT_IN_MB,
      FLAGS_LOCAL_OUTPUT_CACHE_MAX_ITEMS,
      FLAGS_LOCAL_OUTPUT_CACHE_THRESHOLD_ITEMS);

  init_deps_cache.reset();
  init_compiler_info_cache.reset();
  // Show memory just before server loop to understand how much memory is
  // used for initialization.
  handler->TrackMemoryOneshot();

  LOG(INFO) << "server loop start";
  if (server.Loop() != 0) {
    LOG(ERROR) << "Server failed";
    exit(1);
  }
  LOG(INFO) << "server loop end";
  devtools_goma::FlushLogFiles();
  server.StopIPC();
#ifndef _WIN32
  flock(lockfd.fd(), LOCK_UN);
  lockfd.reset(-1);
#else
  lock_fd.Close();
#endif
  LOG(INFO) << "unlock compiler_proxy";
  devtools_goma::FlushLogFiles();
  devtools_goma::SubProcessControllerClient::Get()->Quit();
  devtools_goma::LocalOutputCache::Quit();
  server.Wait();
  handler->Wait();
  devtools_goma::CompilerInfoCache::Quit();
  devtools_goma::DepsCache::Quit();
  devtools_goma::IncludeCache::Quit();
  devtools_goma::ListDirCache::Quit();
  devtools_goma::SubProcessControllerClient::Get()->Shutdown();

  handler.reset();
  wm.Finish();

  if (FLAGS_ENABLE_GLOBAL_FILE_STAT_CACHE ||
      FLAGS_ENABLE_GLOBAL_FILE_ID_CACHE) {
    devtools_goma::GlobalFileStatCache::Quit();
  }

#if HAVE_COUNTERZ
  if (FLAGS_ENABLE_COUNTERZ) {
    devtools_goma::Counterz::Quit();
  }
#endif

  return 0;
}
