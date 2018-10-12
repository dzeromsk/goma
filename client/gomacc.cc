// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


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
#endif

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include "config_win.h"
#endif

#include "basictypes.h"
#include "breakpad.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_specific.h"
#include "cros_util.h"
#include "env_flags.h"
#include "file_stat.h"
#include "glog/logging.h"
#include "goma_ipc.h"
#include "gomacc_argv.h"
#include "gomacc_common.h"
#include "ioutil.h"
#include "mypath.h"
#include "path.h"  // file::JoinPath
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "scoped_fd.h"
#include "simple_timer.h"
#include "subprocess.h"
#include "util.h"

GOMA_DECLARE_bool(DUMP);
GOMA_DECLARE_bool(DUMP_ARGV);
GOMA_DECLARE_bool(FALLBACK);
GOMA_DECLARE_bool(GOMACC_ENABLE_CRASH_DUMP);
GOMA_DECLARE_bool(GOMACC_WRITE_LOG_FOR_TESTING);
GOMA_DECLARE_bool(RETRY);
GOMA_DECLARE_bool(STORE_ONLY);
GOMA_DECLARE_string(TMP_DIR);
GOMA_DECLARE_bool(USE_LOCAL);
GOMA_DECLARE_bool(DISABLED);
GOMA_DECLARE_bool(VERIFY_ASSEMBLER_CODE);
GOMA_DECLARE_bool(VERIFY_PREPROCESS_CODE);
GOMA_DECLARE_string(VERIFY_COMMAND);
#ifdef __linux__
GOMA_DECLARE_string(LOAD_AVERAGE_LIMIT);
GOMA_DECLARE_int32(MAX_SLEEP_TIME);
#endif
#ifdef _WIN32
GOMA_DECLARE_bool(FAN_OUT_EXEC_REQ);
#endif

using devtools_goma::CompilerFlags;
using devtools_goma::CompilerFlagsParser;
using devtools_goma::FileStat;
using devtools_goma::GomaIPC;
using devtools_goma::GetMyDirectory;
using devtools_goma::GetMyPathname;
using devtools_goma::Getpid;
using devtools_goma::GomaClient;
using std::string;

#ifndef _WIN32
using devtools_goma::ReadCommandOutputByPopen;
using devtools_goma::Execvpe;
using devtools_goma::ExecvpeNonGomacc;
#else
using devtools_goma::SpawnAndWait;
using devtools_goma::SpawnAndWaitNonGomacc;

#endif

namespace {

#ifndef _WIN32
// Dump for debugging
string DumpArgvString(
    size_t argc, const char *argv[], const char *message) {
  std::stringstream ss;
  ss << "DEBUG: " << message << ": ";
  for (size_t i = 0; i < argc; ++i) {
    ss << " " << (argv[i] ? argv[i] : "(null)");
  }
  ss << std::endl;
  return ss.str();
}

static void DumpArgv(size_t argc, const char *argv[], const char *message) {
  std::cerr << DumpArgvString(argc, argv, message);
}
#endif

static bool AmIGomacc(absl::string_view argv0) {
  absl::string_view basename = file::Basename(argv0);
  if (basename != "gomacc"
#ifdef _WIN32
      && basename != "gomacc.exe"
#endif
      ) {
    return false;
  }
  return true;
}

bool HandleHttpPortRequest(int argc, char* argv[]) {
  if (argc < 2 || strcmp(argv[1], "port") != 0) {
    return false;
  }
  if (!AmIGomacc(argv[0])) {
    return false;
  }

  GomaIPC::Status status;
  status.health_check_on_timeout = false;
  int port = GetCompilerProxyPort(&status);
  if (port < 0) {
    std::cerr << "GOMA: port request failed. "
              << "connect_success: " << status.connect_success
              << ", err: " << status.err
              << " " << status.error_message
              << ", http_return_code: " << status.http_return_code
              << std::endl;
  } else {
    std::cout << port << std::endl;
  }

  return true;
}

bool HandleGomaTmpDir(int argc, char* argv[]) {
  if (argc < 2 || strcmp(argv[1], "tmp_dir") != 0) {
    return false;
  }
  if (!AmIGomacc(argv[0])) {
    return false;
  }

  std::cout << devtools_goma::GetGomaTmpDir() << std::endl;

  return true;
}

// Runs gomacc again with modification to get preprocessed code (-E) or
// assembler code (-S) instead of object code (-c).
void VerifyIntermediateStageOutput(bool args0_is_argv0,
                                   const std::vector<string>& args,
                                   const char* new_option,
                                   const char* new_ext) {
#ifndef _WIN32
  // Unset GOMA_VERIFY_*_CODE not to run the same thing again.
  unsetenv("GOMA_VERIFY_PREPROCESS_CODE");
  unsetenv("GOMA_VERIFY_ASSEMBLER_CODE");

  string mypath = GetMyPathname();
  std::vector<const char*> new_argv;
  std::vector<string> outputs;
  new_argv.push_back(mypath.c_str());
  bool run_verify_output = false;
  // TODO: refactor CompilerFlags and reuse here.
  // args[0] represents real gcc/g++/javac command.
  // mypath is realpath of argv[0].
  // So, if args[0] is argv[0], we already set it as new_argv[0] (mypath), so
  // we need to skip args[0].
  // Otherwise, if args[0] is not argv[0], it would be invoked via gomacc
  // (e.g. "gomacc gcc .."), so we need to set args[0] in new_argv.
  for (size_t i = (args0_is_argv0 ? 1 : 0); i < args.size(); i++) {
    if (args[i] == "-S" || args[i] == "-E") {
      return;
    }
    if (args[i] == "-c") {
      new_argv.push_back(new_option);
      run_verify_output = true;
      continue;
    }
    if (strncmp(args[i].c_str(), "-M", 2) == 0) {
      if (args[i] == "-MF")
        ++i;  // skip next args. -MF file.
      continue;
    }
    if (args[i] == "-o") {
      if (i + 1 == args.size()) {
        // argument to '-o' is missing.
        return;
      }
      new_argv.push_back("-o");
      ++i;
    } else if (strncmp(args[i].c_str(), "-o", 2) != 0) {
      new_argv.push_back(args[i].c_str());
      continue;
    }
    // args[i] is filename or -ofilename.
    string output = args[i];
    size_t ext = output.find_last_of('.');
    CHECK_NE(ext, string::npos);
    output = output.substr(0, ext) + new_ext;
    outputs.push_back(output);
    new_argv.push_back(outputs.back().c_str());
  }
  if (!run_verify_output)
    return;

  int argc = new_argv.size();
  new_argv.push_back(nullptr);
  if (FLAGS_DUMP_ARGV)
    DumpArgv(argc, &new_argv[0], "verify intermediate");
  pid_t pid = fork();
  if (!pid) {
    // Child process.
    setenv("GOMA_VERIFY_OUTPUT", "true", 1);
    execvp(GetMyPathname().c_str(), const_cast<char**>(&new_argv[0]));
    perror("execvp");
    return;
  }
  if (pid < 0) {
    perror("fork");
    return;
  }
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status)) {
    std::cerr << "VerifyAssemblerCode: exit_status=" << status << std::endl;
  }
#else
  UNREFERENCED_PARAMETER(args0_is_argv0);
  UNREFERENCED_PARAMETER(args);
  UNREFERENCED_PARAMETER(new_option);
  UNREFERENCED_PARAMETER(new_ext);
#endif
}

}  // anonymous namespace

int main(int argc, char* argv[], const char* envp[]) {
#ifdef _WIN32
  DLOG_IF(FATAL, GetModuleHandleW(L"gdi32.dll"))
      << "Error: gdi32.dll found in the process. This will harm performance "
         "and cause hangs. See b/115990434.";
#endif

  CheckFlagNames(envp);

  GOOGLE_PROTOBUF_VERIFY_VERSION;
  // TODO: Check their overhead if they are acceptable.
  //               We might want to eliminate them in release version?
  google::InitGoogleLogging(argv[0]);
#ifndef _WIN32
  google::InstallFailureSignalHandler();
#else
  WinsockHelper wsa;
#endif
  FLAGS_TMP_DIR = devtools_goma::GetGomaTmpDir();
  devtools_goma::CheckTempDirectory(FLAGS_TMP_DIR);
  if (FLAGS_GOMACC_ENABLE_CRASH_DUMP) {
    devtools_goma::InitCrashReporter(devtools_goma::GetCrashDumpDirectory());
  }
  if (FLAGS_GOMACC_WRITE_LOG_FOR_TESTING) {
    LOG(INFO) << "This is a log used by a test that need gomacc.INFO.";
    fprintf(stderr, "log has been written. exiting...\n");
    return 0;
  }

  if (HandleHttpPortRequest(argc, argv)) {
    return 0;
  }
  if (HandleGomaTmpDir(argc, argv)) {
    return 0;
  }

  std::vector<string> args;
  bool masquerade_mode = false;
  string verify_command;
  string local_command_path;
  if (!devtools_goma::BuildGomaccArgv(
          argc, (const char**)argv,
          &args, &masquerade_mode,
          &verify_command, &local_command_path)) {
    // no gcc or g++ in argv.
    fprintf(stderr, "usage: %s [gcc|g++|cl] [options]\n", argv[0]);
#ifndef _WIN32
    const string& goma_ctl = file::JoinPath(GetMyDirectory(), "goma_ctl.py");
    if (system(goma_ctl.c_str())) {
      fprintf(stderr, "Failed to check compiler_proxy's status\n");
    }
#endif
    exit(1);
  }

  if (!verify_command.empty()) {
    FLAGS_VERIFY_COMMAND = verify_command;
    FLAGS_USE_LOCAL = false;
    FLAGS_FALLBACK = false;
    FLAGS_STORE_ONLY = true;
    FLAGS_RETRY = false;
  }

#ifdef __linux__
  // For ChromiumOS.
  if (!devtools_goma::CanGomaccHandleCwd()) {
    FLAGS_DISABLED = true;
  }
#endif

  if (FLAGS_DISABLED) {
    if (masquerade_mode) {
      local_command_path = argv[0];
    }
    // Non absolute path gcc won't be set to local_command_path but it should
    // be set for this time.
    if (local_command_path.empty()) {
      local_command_path = argv[1];
    }

    std::vector<string> envs;
    envs.push_back("GOMA_WILL_FAIL_WITH_UKNOWN_FLAG=true");
    for (int i = 0; envp[i]; ++i)
      envs.push_back(envp[i]);

    FileStat gomacc_filestat(GetMyPathname());
    CHECK(gomacc_filestat.IsValid());

#ifdef __linux__
    // For ChromiumOS.
    // TODO: support other platforms?
    float load = strtof(FLAGS_LOAD_AVERAGE_LIMIT.c_str(), nullptr);
    const absl::Duration max_sleep_time = absl::Seconds(FLAGS_MAX_SLEEP_TIME);
    if (load >= 1.0 && FLAGS_MAX_SLEEP_TIME > 0) {
      devtools_goma::WaitUntilLoadAvgLowerThan(load, max_sleep_time);
    } else {
      LOG(WARNING) << "Will not wait for the low load average because of "
                   << "wrong value."
                   << " FLAGS_LOAD_AVERAGE_LIMIT=" << FLAGS_LOAD_AVERAGE_LIMIT
                   << " FLAGS_MAX_SLEEP_TIME=" << FLAGS_MAX_SLEEP_TIME;
    }
#endif

#ifdef _WIN32
    // Not sure why, but using execve causes accessing of
    // invalid memory address.
    // b/69231578
    // NOTE: SpawnAndWaitNonGomacc is not execve equivalent for windows.
    exit(SpawnAndWaitNonGomacc(&gomacc_filestat, local_command_path, args,
                               envs));
#else
    exit(ExecvpeNonGomacc(&gomacc_filestat, local_command_path, args, envs));
#endif
  }

  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::New(args, "."));
  if (flags.get() == nullptr) {
    // TODO: handle all commands in compiler_proxy
    if (local_command_path.empty()) {
      // masquerade mode with unsupported command name
      // or prepend mode with command basename.
      fprintf(stderr, "usage: %s [gcc|g++|cl] [options]\n", argv[0]);
      exit(1);
    }
    std::vector<string> envs;
    for (int i = 0; envp[i]; ++i)
      envs.push_back(envp[i]);
    // prepend mode with command path.
#ifdef _WIN32
    // Not sure why, but using execve causes accessing of
    // invalid memory address.
    // b/69231578
    // NOTE: SpawnAndWait is not execve equivalent for windows.
    exit(SpawnAndWait(local_command_path, args, envs));
#else
    exit(Execvpe(local_command_path, args, envs));
#endif
  }
  GomaClient client(Getpid(), std::move(flags), envp, local_command_path);

  if (FLAGS_VERIFY_PREPROCESS_CODE) {
    VerifyIntermediateStageOutput(masquerade_mode, args, "-E", ".i");
  }
  if (FLAGS_VERIFY_ASSEMBLER_CODE) {
    VerifyIntermediateStageOutput(masquerade_mode, args, "-S", ".s");
  }

  GomaClient::Result r = client.CallIPC();
  if (r != GomaClient::IPC_OK)
    LOG(ERROR) << "GOMA: compiler proxy not working?";
  int retval = (r != GomaClient::IPC_OK) ? EXIT_FAILURE : client.retval();

  client.OutputResp();
  // normalize exit status code to what could be handled by caller.
  if (retval < 0 || retval > 0xff) {
    return EXIT_FAILURE;
  }

#ifdef _WIN32
  DLOG_IF(FATAL, GetModuleHandleW(L"gdi32.dll"))
      << "Error: gdi32.dll found in the process. This will harm performance "
         "and cause hangs. See b/115990434.";
#endif

  return retval;
}
