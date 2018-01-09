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
    EXPECT_GE(st.GetInNanoSeconds(), 0);
    EXPECT_GE(st.Get(), 0.0);

    // The second call should have a larger time.
    long long t1 = st.GetInNanoSeconds();
    long long t2 = st.GetInNanoSeconds();
    EXPECT_GE(t2, t1);
  }
}

}  // namespace devtools_goma
