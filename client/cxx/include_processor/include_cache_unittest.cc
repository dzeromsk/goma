// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "include_cache.h"

#include <algorithm>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "content.h"
#include "file_stat.h"
#include "file_stat_cache.h"
#include "goma_hash.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

class IncludeCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    IncludeCache::Init(2, true);  // can keep only 2 items.
  }

  void TearDown() override {
    IncludeCache::Quit();
  }

  int Size(IncludeCache* include_cache) const {
    return include_cache->cache_items_.size();
  }

  size_t HitCount(IncludeCache* include_cache) const {
    return include_cache->hit_count_.value();
  }
  size_t MissedCount(IncludeCache* include_cache) const {
    return include_cache->missed_count_.value();
  }
};

TEST_F(IncludeCacheTest, GetDirectiveList) {
  IncludeCache* ic = IncludeCache::instance();

  TmpdirUtil tmpdir("includecache");
  string ah = tmpdir.FullPath("a.h");
  string content = "#include <stdio.h>\n";
  tmpdir.CreateTmpFile("a.h", content);

  FileStat file_stat;
  file_stat.size = content.size();
  file_stat.mtime = absl::FromTimeT(100);

  size_t hit_count_0 = HitCount(ic);
  size_t missed_count_0 = MissedCount(ic);

  (void)ic->GetIncludeItem(ah, file_stat);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 1, MissedCount(ic));

  // Reload
  (void)ic->GetIncludeItem(ah, file_stat);
  EXPECT_EQ(hit_count_0 + 1, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 1, MissedCount(ic));

  // Reload with mtime
  file_stat.mtime = absl::FromTimeT(105);
  (void)ic->GetIncludeItem(ah, file_stat);
  EXPECT_EQ(hit_count_0 + 1, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 2, MissedCount(ic));
}

TEST_F(IncludeCacheTest, SetGetIfMaxSizeIsZero) {
  // Initialize with max size is 0.
  IncludeCache::Quit();
  IncludeCache::Init(0, true);

  IncludeCache* ic = IncludeCache::instance();

  TmpdirUtil tmpdir("includecache");
  string ah = tmpdir.FullPath("a.h");
  string content = "#include <stdio.h>\n";
  tmpdir.CreateTmpFile("a.h", content);

  FileStat file_stat;
  file_stat.size = content.size();
  file_stat.mtime = absl::FromTimeT(100);

  size_t hit_count_0 = HitCount(ic);
  size_t missed_count_0 = MissedCount(ic);

  (void)ic->GetIncludeItem(ah, file_stat);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 1, MissedCount(ic));

  // Reload (missed due to evicted)
  (void)ic->GetIncludeItem(ah, file_stat);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 2, MissedCount(ic));

  // Reload with mtime
  file_stat.mtime = absl::FromTimeT(105);
  (void)ic->GetIncludeItem(ah, file_stat);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 3, MissedCount(ic));
}

TEST_F(IncludeCacheTest, ExceedMemory) {
  IncludeCache* ic = IncludeCache::instance();

  TmpdirUtil tmpdir("includecache");

  std::vector<string> paths;
  std::vector<string> contents;
  std::vector<FileStat> file_stats;
  for (size_t i = 0; i < 3; ++i) {
    string filename = absl::StrCat("a", i, ".h");
    string path = tmpdir.FullPath(filename);
    string content = "content";
    tmpdir.CreateTmpFile(filename, content);

    FileStat file_stat;
    file_stat.size = content.size();
    file_stat.mtime = absl::FromTimeT(100);

    paths.push_back(std::move(path));
    contents.push_back(std::move(content));
    file_stats.push_back(std::move(file_stat));
  }

  size_t hit_count_0 = HitCount(ic);
  size_t missed_count_0 = MissedCount(ic);

  (void)ic->GetIncludeItem(paths[0], file_stats[0]);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 1, MissedCount(ic));
  EXPECT_EQ(1, Size(ic));

  (void)ic->GetIncludeItem(paths[1], file_stats[1]);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 2, MissedCount(ic));
  EXPECT_EQ(2, Size(ic));

  (void)ic->GetIncludeItem(paths[2], file_stats[2]);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 3, MissedCount(ic));
  EXPECT_EQ(2, Size(ic));

  // Reload [0]. It must have been evicted.
  (void)ic->GetIncludeItem(paths[0], file_stats[0]);
  EXPECT_EQ(hit_count_0, HitCount(ic));
  EXPECT_EQ(missed_count_0 + 4, MissedCount(ic));
  EXPECT_EQ(2, Size(ic));
}

TEST_F(IncludeCacheTest, GetDirectiveHash)
{
  IncludeCache* ic = IncludeCache::instance();

  TmpdirUtil tmpdir("includecache");
  const string& ah = tmpdir.FullPath("a.h");
  const string& bh = tmpdir.FullPath("b.h");
  tmpdir.CreateTmpFile("a.h",
                       "#include <stdio.h>\n");
  tmpdir.CreateTmpFile("b.h",
                       "#include <math.h>\n");

  {
    SHA256HashValue hash_expected;
    ComputeDataHashKeyForSHA256HashValue("#include <stdio.h>\n",
                                         &hash_expected);

    FileStatCache file_stat_cache;
    FileStat file_stat(file_stat_cache.Get(ah));
    ASSERT_TRUE(file_stat.IsValid());

    absl::optional<SHA256HashValue> hash_actual =
        ic->GetDirectiveHash(ah, file_stat);
    EXPECT_TRUE(hash_actual.has_value());
    EXPECT_EQ(hash_expected, hash_actual.value());
  }

  // Update file content
  tmpdir.CreateTmpFile("a.h",
                       "#include <string.h>\n");
  {
    SHA256HashValue hash_expected;
    ComputeDataHashKeyForSHA256HashValue("#include <string.h>\n",
                                         &hash_expected);

    FileStatCache file_stat_cache;
    FileStat file_stat(file_stat_cache.Get(ah));
    ASSERT_TRUE(file_stat.IsValid());

    absl::optional<SHA256HashValue> hash_actual =
        ic->GetDirectiveHash(ah, file_stat);
    EXPECT_TRUE(hash_actual.has_value());
    EXPECT_EQ(hash_expected, hash_actual.value());
  }

  // Currently IncludeCache does not have a cache for b.h.
  // However, GetDirectiveHash will succeed, and the cached result
  // will be stored.
  {
    SHA256HashValue hash_expected;
    ComputeDataHashKeyForSHA256HashValue("#include <math.h>\n", &hash_expected);

    FileStatCache file_stat_cache;
    FileStat file_stat(file_stat_cache.Get(bh));
    ASSERT_TRUE(file_stat.IsValid());

    absl::optional<SHA256HashValue> hash_actual =
        ic->GetDirectiveHash(bh, file_stat);
    EXPECT_TRUE(hash_actual.has_value());
    EXPECT_EQ(hash_expected, hash_actual.value());

    size_t hit_count_before = HitCount(ic);
    (void)ic->GetIncludeItem(bh, file_stat);
    size_t hit_count_after = HitCount(ic);
    EXPECT_EQ(hit_count_before + 1, hit_count_after);
  }
}

TEST_F(IncludeCacheTest, DumpEmpty) {
  IncludeCache* ic = IncludeCache::instance();

  std::ostringstream ss;
  // CHECK should not be triggered.
  ic->Dump(&ss);
}

}  // namespace devtools_goma
