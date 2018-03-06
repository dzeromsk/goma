// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "breakpad.h"

#include <signal.h>

#include <string>

#include "client/windows/handler/exception_handler.h"
#include "glog/logging.h"

namespace {

bool g_is_crash_reporter_enabled = false;
google_breakpad::ExceptionHandler* g_breakpad = nullptr;

// TODO: revise followings when glog supports wchar_t*.
std::ostream& operator<<(std::ostream& out, const wchar_t* str) {
  std::wstring wide(str);
  out << std::string(wide.begin(), wide.end());
  return out;
}


bool DumpCallback(const wchar_t* dump_dir,
                  const wchar_t* minidump_id,
                  void*,
                  EXCEPTION_POINTERS*,
                  MDRawAssertionInfo*,
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

// This function is copied from Chromium's base/win/win_util.cc.
void __cdecl ForceCrashOnSigAbort(int) {
  *((volatile int*)0) = 0x1337;
}

// This function is copied from Chromium's base/win/win_util.cc.
void SetAbortBehaviorForCrashReporting() {
  // Prevent CRT's abort code from prompting a dialog or trying to "report" it.
  // Disabling the _CALL_REPORTFAULT behavior is important since otherwise it
  // has the sideffect of clearing our exception filter, which means we
  // don't get any crash.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

  // Set a SIGABRT handler for good measure. We will crash even if the default
  // is left in place, however this allows us to crash earlier. And it also
  // lets us crash in response to code which might directly call raise(SIGABRT)
  signal(SIGABRT, ForceCrashOnSigAbort);
}

}  // namespace

namespace devtools_goma {

void InitCrashReporter(const std::string& dump_output_dir) {
  g_is_crash_reporter_enabled = true;

  DCHECK(!g_breakpad);
  std::wstring wide_dump_dir(dump_output_dir.begin(), dump_output_dir.end());
  g_breakpad = new google_breakpad::ExceptionHandler(
    wide_dump_dir, nullptr, DumpCallback, nullptr,
    google_breakpad::ExceptionHandler::HANDLER_ALL);

  SetAbortBehaviorForCrashReporting();

  atexit(CleanUpReporter);
}

bool IsCrashReporterEnabled() {
  return g_is_crash_reporter_enabled;
}

}  // namespace devtools_goma
