// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "named_pipe_client_win.h"

#include <cstdlib>

#include <glog/logging.h>

#include "simple_timer.h"
#include "platform_thread.h"

namespace devtools_goma {

NamedPipeFactory::NamedPipeFactory(const std::string& name,
                                   int timeout_ms)
    : name_(name), timeout_ms_(timeout_ms) {
}

NamedPipeFactory::~NamedPipeFactory() {
}

ScopedNamedPipe NamedPipeFactory::New() {
  std::string pipename = "\\\\.\\pipe\\" + name_;
  SimpleTimer t;

  for (;;) {
    int left_time = timeout_ms_ - t.GetInIntMilliseconds();
    if (left_time <= 0) {
      break;
    }

    if (!WaitNamedPipeA(pipename.c_str(), left_time)) {
      DWORD last_error = GetLastError();
      if (last_error == ERROR_SEM_TIMEOUT) {
        LOG(ERROR) << "Timed-out to WaitNamedPipe " << pipename
                   << " with timeout_ms=" << timeout_ms_
                   << ", passed " << t.GetInMilliseconds() << " ms."
                   << " Please consider to specify longer timeout by"
                   << " setting GOMA_NAMEDPIPE_WAIT_TIMEOUT_MS envvar"
                   << " before `gn gen` or invoking gomacc directly."
                   << " b/70640154";
      }
      LOG_SYSRESULT(last_error);
      return ScopedNamedPipe();
    }

    ScopedNamedPipe pipe(CreateFileA(pipename.c_str(),
                                     GENERIC_READ | GENERIC_WRITE,
                                     0,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_FLAG_OVERLAPPED,
                                     nullptr));
    if (!pipe.valid()) {
      DWORD last_error = GetLastError();
      if (last_error == ERROR_PIPE_BUSY) {
        continue;
      }
      LOG_SYSRESULT(GetLastError());
      return ScopedNamedPipe();
    }
    return pipe;
  }
  LOG(ERROR) << "Timed-out to create new pipe:" << pipename;
  return ScopedNamedPipe();
}

}  // namespace devtools_goma
