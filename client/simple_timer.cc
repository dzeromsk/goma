// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_timer.h"

// SimpleTimer::Start() and SimpleTimer::GetInNanoSeconds() are
// platform specific. See simple_timer_*.cc.

namespace devtools_goma {

SimpleTimer::SimpleTimer(CtorFlag cf) {
  if (cf == START) {
    Start();
  }
}

SimpleTimer::SimpleTimer() {
  Start();
}

SimpleTimer::~SimpleTimer() {}

// Return elapsed time in seconds.
double SimpleTimer::Get() const {
  return GetInNanoSeconds() / 1000000000.0;
}

// Return elapsed time in milliseconds.
int SimpleTimer::GetInMs() const {
  return static_cast<int>(GetInMilliSeconds());
}

long long SimpleTimer::GetInMilliSeconds() const {
  return GetInNanoSeconds() / 1000000;
}

}  // namespace devtools_goma
