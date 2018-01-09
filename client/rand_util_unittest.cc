// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rand_util.h"

#include <cctype>

#include "gtest/gtest.h"

namespace devtools_goma {

TEST(RandUtil, GetRandomAlphanumeric) {
  const size_t kSize = 128;
  std::string rnd = GetRandomAlphanumeric(kSize);

  for (const auto& c : rnd) {
    EXPECT_TRUE(std::isalnum(c));
  }
  EXPECT_EQ(kSize, rnd.size());
}

}  // namespace devtools_goma
