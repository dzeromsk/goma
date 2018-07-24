// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "filetime_win.h"

#include "glog/logging.h"

namespace devtools_goma {

absl::Time ConvertFiletimeToAbslTime(const FILETIME& filetime) {
  ULARGE_INTEGER ull;
  ull.LowPart = filetime.dwLowDateTime;
  ull.HighPart = filetime.dwHighDateTime;

  constexpr int64_t kDeltaEpochIn100NsBlocks = DELTA_EPOCH_IN_MICROSECS * 10;
  int64_t unix_time_in_100_ns_blocks = ull.QuadPart - kDeltaEpochIn100NsBlocks;

  // Make sure that the number of nanoseconds since the Unix Epoch does not
  // overflow the int64_t type.
  CHECK_GT(unix_time_in_100_ns_blocks * 100, unix_time_in_100_ns_blocks);

  return absl::FromUnixNanos(unix_time_in_100_ns_blocks * 100);
}

}  // namespace devtools_goma
