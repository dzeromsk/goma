// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "named_pipe_client_win.h"

#include <cstdlib>

#include <glog/logging.h>

#include "absl/base/macros.h"
#include "platform_thread.h"
#include "simple_timer.h"

namespace devtools_goma {

NamedPipeFactory::NamedPipeFactory(const std::string& name,
                                   absl::Duration timeout)
    : name_(name), timeout_(timeout) {
}

NamedPipeFactory::~NamedPipeFactory() {
}

ScopedNamedPipe NamedPipeFactory::New() {
  std::string pipename = "\\\\.\\pipe\\" + name_;
  SimpleTimer timer;

  for (;;) {
    absl::Duration time_remaining = timeout_ - timer.GetDuration();
    if (time_remaining <= absl::ZeroDuration()) {
      break;
    }

    if (!WaitNamedPipeA(pipename.c_str(),
                        absl::ToInt64Milliseconds(time_remaining))) {
      DWORD last_error = GetLastError();
      if (last_error == ERROR_SEM_TIMEOUT) {
        LOG(ERROR) << "Timed-out to WaitNamedPipe " << pipename
                   << " with timeout=" << timeout_
                   << ", passed " << timer.GetDuration()
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
