// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "simple_timer.h"

#include <gtest/gtest.h>

namespace devtools_goma {

// smoke test to ensure SimpleTimer does not return minus value.
TEST(SimpleTimer, smoke) {
  SimpleTimer st(SimpleTimer::START);

  for (int i = 0; i < 1000; ++i) {
    EXPECT_GE(st.GetInNanoseconds(), 0);
    EXPECT_GE(st.GetInSeconds(), 0.0);
    EXPECT_GE(st.GetDuration(), absl::ZeroDuration());

    // The second call should have a larger time.
    long long t1 = st.GetInNanoseconds();
    long long t2 = st.GetInNanoseconds();
    EXPECT_GE(t2, t1);
  }
}

}  // namespace devtools_goma
