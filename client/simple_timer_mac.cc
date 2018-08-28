// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_timer.h"

#include <mach/mach_time.h>

#include <type_traits>

#include "glog/logging.h"

namespace {
int64_t MachToNanoSec(uint64_t mach_diff_time) {
  // code from chromium/src/base/time/time_mac.cc

  static mach_timebase_info_data_t timebase_info;
  static_assert(std::is_trivially_destructible<mach_timebase_info_data_t>::value,
                "mach_timebase_info_data_t must be trivially destructible");
  if (timebase_info.denom == 0) {
    // Zero-initialization of statics guarantees that denom will be 0 before
    // calling mach_timebase_info.  mach_timebase_info will never set denom to
    // 0 as that would be invalid, so the zero-check can be used to determine
    // whether mach_timebase_info has already been called.  This is
    // recommended by Apple's QA1398.
    kern_return_t kr = mach_timebase_info(&timebase_info);
    CHECK_EQ(kr, KERN_SUCCESS);
    CHECK_NE(0UL, timebase_info.denom);
  }

  // numer and denom are both expected to be 1.
  uint64_t result = mach_diff_time;
  result *= timebase_info.numer;
  result /= timebase_info.denom;
  return static_cast<int64_t>(result);
}
}  // anonymous namespace

namespace devtools_goma {

void SimpleTimer::Start() {
  // mach_absolute_time is monotonic.
  start_time_ = mach_absolute_time();
}

int64_t SimpleTimer::GetInNanoSeconds() const {
  uint64_t end_time = mach_absolute_time();

  DCHECK_LE(start_time_, end_time);
  if (end_time < start_time_) {
    // This shouldn't happen, but check.
    LOG(ERROR) << "SimpleTimer is not monotonic:"
               << " start_time=" << start_time_
               << " end_time=" << end_time;
    return 0;
  }
  return MachToNanoSec(end_time - start_time_);
}

}  // namespace devtools_goma
