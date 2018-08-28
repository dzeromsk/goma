// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_timer.h"

// SimpleTimer::Start() and SimpleTimer::GetInNanoSeconds() are
// platform specific. See simple_timer_*.cc.

namespace devtools_goma {

absl::Duration SimpleTimer::GetDuration() const {
  return absl::Nanoseconds(GetInNanoSeconds());
}

}  // namespace devtools_goma
