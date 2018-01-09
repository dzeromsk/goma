// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GET_TIMESTAMP_H_
#define DEVTOOLS_GOMA_CLIENT_GET_TIMESTAMP_H_

#ifndef _WIN32
#include <sys/time.h>
#else
#include "gettimeofday_helper_win.h"
#endif
#include <time.h>

#include "timestamp.h"

namespace devtools_goma {

millitime_t GetCurrentTimestampMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<millitime_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GET_TIMESTAMP_H_
