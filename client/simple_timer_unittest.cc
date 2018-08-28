// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_timer.h"

#include <gtest/gtest.h>

namespace devtools_goma {

// Smoke test to ensure SimpleTimer does not return a negative value.
TEST(SimpleTimer, Smoke) {
  SimpleTimer st(SimpleTimer::START);

  for (int i = 0; i < 1000; ++i) {
    EXPECT_GE(st.GetDuration(), absl::ZeroDuration());

    // The second call should have a larger time.
    absl::Duration t1 = st.GetDuration();
    absl::Duration t2 = st.GetDuration();
    EXPECT_GE(t2, t1);
  }
}

}  // namespace devtools_goma
