// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sha256_hash_cache.h"

#include <gtest/gtest.h>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "autolock_timer.h"
#include "unittest_util.h"

namespace devtools_goma {

class SHA256HashCacheTest : public testing::Test {
 public:

 protected:
  SHA256HashCache cache_;
};

TEST_F(SHA256HashCacheTest, BasicTest) {
  TmpdirUtil tmpdir("sha256_hash_cache");

  tmpdir.CreateEmptyFile("empty");
  const std::string& empty = tmpdir.FullPath("empty");

  // Set old timestamp.
  UpdateMtime(empty, absl::Now() - absl::Seconds(2));

  std::string hash;

  // cache miss
  EXPECT_TRUE(cache_.GetHashFromCacheOrFile(empty, &hash));

  EXPECT_EQ(1, cache_.total());
  EXPECT_EQ(0, cache_.hit());

  // cache miss, invalid file
  EXPECT_FALSE(
      cache_.GetHashFromCacheOrFile(tmpdir.FullPath("not_exist"), &hash));

  EXPECT_EQ(2, cache_.total());
  EXPECT_EQ(0, cache_.hit());

  // Set future time.
  auto empty_file_stat = FileStat(empty);
  ASSERT_TRUE(empty_file_stat.mtime.has_value());

  // cache hit
  EXPECT_TRUE(cache_.GetHashFromCacheOrFile(empty, &hash));

  EXPECT_EQ(3, cache_.total());
  EXPECT_EQ(1, cache_.hit());
}

}  // namespace devtools_goma
