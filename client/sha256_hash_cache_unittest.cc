// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sha256_hash_cache.h"

#include <gtest/gtest.h>

#include "absl/time/clock.h"
#include "autolock_timer.h"
#include "unittest_util.h"

namespace devtools_goma {

class SHA256HashCacheTest : public testing::Test {
 public:
  SHA256HashCacheTest() { cache_.time_fn_ = Time; }

 protected:
  static time_t Time(time_t*) { return time_; }

  static time_t time_;
  SHA256HashCache cache_;
};

time_t SHA256HashCacheTest::time_ = 0;

TEST_F(SHA256HashCacheTest, BasicTest) {
  TmpdirUtil tmpdir("sha256_hash_cache");

  tmpdir.CreateEmptyFile("empty");
  const std::string& empty = tmpdir.FullPath("empty");

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
  time_ = FileStat(empty).mtime + 2;

  // cache hit
  EXPECT_TRUE(cache_.GetHashFromCacheOrFile(empty, &hash));

  EXPECT_EQ(3, cache_.total());
  EXPECT_EQ(1, cache_.hit());
}

}  // namespace devtools_goma
