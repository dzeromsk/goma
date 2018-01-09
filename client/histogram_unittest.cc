// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "histogram.h"

#include <gtest/gtest.h>

namespace devtools_goma {

TEST(HistogramTest, DetermineBucket)
{
  Histogram histogram;

  EXPECT_EQ(0, histogram.DetermineBucket(0));
  EXPECT_EQ(1, histogram.DetermineBucket(1));
  EXPECT_EQ(2, histogram.DetermineBucket(2));
  EXPECT_EQ(2, histogram.DetermineBucket(3));
  EXPECT_EQ(3, histogram.DetermineBucket(4));
  EXPECT_EQ(3, histogram.DetermineBucket(5));
  EXPECT_EQ(3, histogram.DetermineBucket(6));
  EXPECT_EQ(3, histogram.DetermineBucket(7));
  EXPECT_EQ(4, histogram.DetermineBucket(8));
  EXPECT_EQ(4, histogram.DetermineBucket(9));

  // Negative value will be treated as 0.
  EXPECT_EQ(0, histogram.DetermineBucket(-1));
  EXPECT_EQ(0, histogram.DetermineBucket(-100));
}

TEST(HistogramTest, BucketValue)
{
  Histogram histogram;

  EXPECT_EQ(0, histogram.BucketValue(0));
  EXPECT_EQ(1, histogram.BucketValue(1));
  EXPECT_EQ(2, histogram.BucketValue(2));
  EXPECT_EQ(4, histogram.BucketValue(3));
  EXPECT_EQ(8, histogram.BucketValue(4));
  EXPECT_EQ(16, histogram.BucketValue(5));

  EXPECT_EQ(0, histogram.BucketValue(-1));
  EXPECT_EQ(0, histogram.BucketValue(-100));
}

} // namespace devtools_goma
