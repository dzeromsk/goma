// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_timer.h"

#include "glog/logging.h"

namespace devtools_goma {

void SimpleTimer::Start() {
  ::QueryPerformanceCounter(&start_time_);
  ::QueryPerformanceFrequency(&frequency_);
}

// In chromium's base/time/time_win.cc, QPC is not used in some conditions.
// But we assume that goma users use goma on machines QPC works correctly.
int64_t SimpleTimer::GetInNanoSeconds() const {
  LARGE_INTEGER end_time;
  ::QueryPerformanceCounter(&end_time);

  DCHECK_LE(start_time_.QuadPart, end_time.QuadPart);
  if (end_time.QuadPart < start_time_.QuadPart) {
    // This shouldn't happen, but check.
    LOG(ERROR) << "SimpleTimer is not monotonic: "
               << " start_time=" << start_time_.QuadPart
               << " end_time=" << end_time.QuadPart;
    return 0;
  }

  // https://msdn.microsoft.com/en-us/library/windows/desktop/dn553408(v=vs.85).aspx
  double diff = end_time.QuadPart - start_time_.QuadPart;
  diff *= 1000000000;
  diff /= frequency_.QuadPart;
  return diff;
}

}  // namespace devtools_goma
