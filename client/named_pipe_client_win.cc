// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "named_pipe_client_win.h"

#include <cstdlib>

#include <glog/logging.h>

#include "simple_timer.h"
#include "platform_thread.h"

namespace devtools_goma {

NamedPipeFactory::NamedPipeFactory(const std::string& name)
    : name_(name) {
}

NamedPipeFactory::~NamedPipeFactory() {
}

ScopedNamedPipe NamedPipeFactory::New() {
  std::string pipename = "\\\\.\\pipe\\" + name_;
  // TODO: This is mitigation for b/36493466
  const int kTimeoutMillisec = 13 * 1000;
  SimpleTimer t;

  for (;;) {
    int left_time = kTimeoutMillisec - t.GetInMs();
    if (left_time <= 0) {
      break;
    }

    if (!WaitNamedPipeA(pipename.c_str(), left_time)) {
      DWORD last_error = GetLastError();
      if (last_error == ERROR_SEM_TIMEOUT) {
        LOG(ERROR) << "Timed-out to WaitNamedPipe " << pipename
                   << " with timeout_ms=" << kTimeoutMillisec;
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
