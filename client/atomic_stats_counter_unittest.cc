// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "atomic_stats_counter.h"

#include <gtest/gtest.h>

namespace devtools_goma {

// TODO: Write multithread tests.
// TODO: Write performance tests.

TEST(StatsCounterTest, Basic) {
  StatsCounter sc;
  EXPECT_EQ(0, sc.value());
  sc.Add(1);
  EXPECT_EQ(1, sc.value());
  sc.Add(2);
  EXPECT_EQ(3, sc.value());
  sc.Clear();
  EXPECT_EQ(0, sc.value());
}

TEST(StatsCounterTest, Int32overflow) {
  StatsCounter sc;
  sc.Add(0x7FFFFFFFLL);
  EXPECT_EQ(0x7FFFFFFFLL, sc.value());
  sc.Add(1);
  EXPECT_EQ(0x80000000LL, sc.value());
  sc.Add(0x80000000LL);
  EXPECT_EQ(0x100000000LL, sc.value());
  sc.Clear();

  sc.Add(0x7FFFFFFFFFFFLL);
  EXPECT_EQ(0x7FFFFFFFFFFFLL, sc.value());
}

}  // namespace devtools_goma
