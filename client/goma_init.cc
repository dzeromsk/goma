// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "goma_init.h"

#include <iostream>

#include "autolock_timer.h"
#include "compiler_proxy_info.h"
#include "env_flags.h"
#include "glog/logging.h"
#include "google/protobuf/stubs/logging.h"
#include "mypath.h"
#include "ioutil.h"

using std::string;

namespace {

void ProtobufLogHandler(google::protobuf::LogLevel level,
                        const char* filename,
                        int line,
                        const string& message) {
  // Convert protobuf log level to glog log severity.
  int severity = google::GLOG_ERROR;
  switch (level) {
  case google::protobuf::LOGLEVEL_INFO:
    severity = google::GLOG_INFO;
    break;
  case google::protobuf::LOGLEVEL_WARNING:
    severity = google::GLOG_WARNING;
    break;
  case google::protobuf::LOGLEVEL_ERROR:
    severity = google::GLOG_ERROR;
    break;
  case google::protobuf::LOGLEVEL_FATAL:
    severity = google::GLOG_FATAL;
    break;
  }

  google::LogMessage(filename, line, severity).stream() << message;
}

}  // anonymous namespace

namespace devtools_goma {

void Init(int argc, char* argv[], const char* envp[]) {
  CheckFlagNames(envp);
  AutoConfigureFlags(envp);

  // Display version string and exit if --version is specified.
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    std::cout << "GOMA version " << kBuiltRevisionString << std::endl;
    exit(0);
  }
  if (argc == 2 && strcmp(argv[1], "--build-info") == 0) {
    std::cout << kUserAgentString << std::endl;
    exit(0);
  }
#ifndef NO_AUTOLOCK_STAT
  if (FLAGS_ENABLE_CONTENTIONZ)
    g_auto_lock_stats = new AutoLockStats;
#endif

  const string username = GetUsernameNoEnv();
  if (username != GetUsernameEnv()) {
    LOG(ERROR) << "username mismatch: " << username
               << " env:" << GetUsernameEnv();
  }

  FLAGS_TMP_DIR = GetGomaTmpDir();
  CheckTempDirectory(FLAGS_TMP_DIR);
}

void InitLogging(const char* argv0) {
  google::InitGoogleLogging(argv0);
  // Sets log hanlder for protobuf/logging so that protobuf outputs log
  // to where GLOG is outputting.
  google::protobuf::SetLogHandler(ProtobufLogHandler);
#ifndef _WIN32
  google::InstallFailureSignalHandler();
#endif
  LOG(INFO) << "goma built revision " << kBuiltRevisionString;
#ifndef NDEBUG
  LOG(ERROR) << "WARNING: DEBUG BINARY -- Performance may suffer";
#endif
#ifdef ADDRESS_SANITIZER
  LOG(ERROR) << "WARNING: ASAN BINARY -- Performance may suffer";
#endif
  {
    std::ostringstream ss;
    DumpEnvFlag(&ss);
    LOG(INFO) << "goma flags:" << ss.str();
  }
  FlushLogFiles();
}

}  // namespace devtools_goma
