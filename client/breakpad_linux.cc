// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "breakpad.h"

#include "client/linux/handler/exception_handler.h"
#include "compiler_specific.h"
#include "glog/logging.h"

namespace {

using google_breakpad::ExceptionHandler;
using google_breakpad::MinidumpDescriptor;

bool g_is_crash_reporter_enabled = false;
ExceptionHandler* g_breakpad = nullptr;

bool DumpCallback(const google_breakpad::MinidumpDescriptor& descriptor,
                  void* context ALLOW_UNUSED,
                  bool succeeded) {
  LOG(INFO) << "Crash Dump path: " << descriptor.path()
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

void InitCrashReporter(const string& dump_output_dir) {
  g_is_crash_reporter_enabled = true;

  DCHECK(!g_breakpad);
  MinidumpDescriptor descriptor(dump_output_dir);
  g_breakpad = new ExceptionHandler(descriptor,
                                    nullptr,
                                    DumpCallback,
                                    nullptr,
                                    true,  // Install handlers.
                                    -1);   // Server file descriptor.
  atexit(CleanUpReporter);
}

bool IsCrashReporterEnabled() {
  return g_is_crash_reporter_enabled;
}

}  // namespace devtools_goma
