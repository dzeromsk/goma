// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// TODO: Rewrite with obj-C++ and use Breakpad framework instead
//                    of exception_handler.h.  I know it requires changes on
//                    the way we handles crash dump.

#include "breakpad.h"

#include "base/compiler_specific.h"
#include "client/mac/handler/exception_handler.h"
#include "glog/logging.h"

namespace {

bool g_is_crash_reporter_enabled = false;
google_breakpad::ExceptionHandler* g_breakpad = nullptr;

bool DumpCallback(const char* dump_dir,
                  const char* minidump_id,
                  void* context ALLOW_UNUSED,
                  bool succeeded) {
  LOG(INFO) << "Crash Dump dir: " << dump_dir
            << " minidump_id=" << minidump_id
            << " succeeded=" << succeeded;
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
  google::FlushLogFilesUnsafe(google::INFO);
#else
  google::FlushLogFilesUnsafe(google::GLOG_INFO);
#endif
  return succeeded;
}

void CleanUpReporter() {
  g_is_crash_reporter_enabled = false;
  delete g_breakpad;
}

}  // namespace

namespace devtools_goma {

void InitCrashReporter(const std::string& dump_output_dir) {
  g_is_crash_reporter_enabled = true;

  DCHECK(!g_breakpad);
  g_breakpad = new google_breakpad::ExceptionHandler(dump_output_dir,
                                                     nullptr,
                                                     DumpCallback,
                                                     nullptr,
                                                     true,
                                                     nullptr);
  atexit(CleanUpReporter);
}

bool IsCrashReporterEnabled() {
  return g_is_crash_reporter_enabled;
}

}  // namespace devtools_goma
