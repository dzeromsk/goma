// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_timer.h"

// SimpleTimer::Start() and SimpleTimer::GetInNanoseconds() are
// platform specific. See simple_timer_*.cc.

namespace devtools_goma {

// Return elapsed time in seconds.
int SimpleTimer::GetInIntMilliseconds() const {
  return static_cast<int>(GetInMilliseconds());
}

double SimpleTimer::GetInSeconds() const {
  return GetInNanoseconds() / 1000000000.0;
}

// Return elapsed time in milliseconds.
long long SimpleTimer::GetInMilliseconds() const {
  return GetInNanoseconds() / 1000000;
}

}  // namespace devtools_goma
