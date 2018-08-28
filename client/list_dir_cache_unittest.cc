// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "list_dir_cache.h"

#include <memory>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "path.h"
#include "unittest_util.h"

namespace devtools_goma {

namespace {

// Dir name for testing.
constexpr absl::string_view kInnerDirName = "dir";

bool CompareDirEntry(const DirEntry& a, const DirEntry& b) {
  return a.name < b.name;
}

}  // namespace

class ListDirCacheTest : public ::testing::Test {
 public:
  ListDirCacheTest() = default;
  ~ListDirCacheTest() override = default;

  void SetUp() override {
    ListDirCache::Init(1024);

    tmp_dir_ = absl::make_unique<TmpdirUtil>("list_dir_cache_test");
    tmp_dir_->CreateEmptyFile(file::JoinPath(std::string(kInnerDirName), "a"));
    tmp_dir_->CreateEmptyFile(file::JoinPath(std::string(kInnerDirName), "b"));
  }

  void TearDown() override {
    tmp_dir_.reset();

    ListDirCache::Quit();
  }

  const std::string GetTestDirPath() const {
    return tmp_dir_->FullPath(std::string(kInnerDirName));
  }

  void CreateFileInTestDir(const std::string& filename) {
    tmp_dir_->CreateEmptyFile(
        file::JoinPath(std::string(kInnerDirName), filename));
  }

  ListDirCache* Cache() {
    return ListDirCache::instance();
  }

 private:
  // Holds a directory used as a test input.
  std::unique_ptr<TmpdirUtil> tmp_dir_;
};

TEST_F(ListDirCacheTest, NoActions) {
  EXPECT_EQ(0, Cache()->hit());
  EXPECT_EQ(0, Cache()->miss());
}

TEST_F(ListDirCacheTest, NonexistentDir) {
  std::vector<DirEntry> entries;

  // No hit - directory does not exist. Pass in an uninitialized FileStat.
  EXPECT_FALSE(Cache()->GetDirEntries("notexist", FileStat(), &entries));
  EXPECT_EQ(0, Cache()->hit());
  EXPECT_EQ(1, Cache()->miss());
}

TEST_F(ListDirCacheTest, FirstTimeCacheMiss) {
  std::vector<DirEntry> entries;
  const auto path = GetTestDirPath();
  FileStat file_stat(path);

  EXPECT_TRUE(Cache()->GetDirEntries(path, file_stat, &entries));
  std::sort(entries.begin(), entries.end(), CompareDirEntry);

  // No hit on first access.
  EXPECT_EQ(0, Cache()->hit());
  EXPECT_EQ(1, Cache()->miss());

  ASSERT_EQ(4, entries.size());
  EXPECT_EQ(".", entries[0].name);
  EXPECT_EQ("..", entries[1].name);
  EXPECT_EQ("a", entries[2].name);
  EXPECT_EQ("b", entries[3].name);
}

TEST_F(ListDirCacheTest, SecondTimeCacheHit) {
  std::vector<DirEntry> entries1, entries2;
  const string path = GetTestDirPath();
  FileStat file_stat(path);

  // Decrease mtime for cache hit in second attempt.
  ASSERT_TRUE(file_stat.mtime.has_value());
  *file_stat.mtime -= absl::Seconds(2);

  EXPECT_TRUE(Cache()->GetDirEntries(path, file_stat, &entries1));

  EXPECT_TRUE(Cache()->GetDirEntries(path, file_stat, &entries2));
  std::sort(entries2.begin(), entries2.end(), CompareDirEntry);

  EXPECT_EQ(1, Cache()->hit());
  EXPECT_EQ(1, Cache()->miss());

  ASSERT_EQ(4, entries2.size());
  EXPECT_EQ(".", entries2[0].name);
  EXPECT_EQ("..", entries2[1].name);
  EXPECT_EQ("a", entries2[2].name);
  EXPECT_EQ("b", entries2[3].name);
}

TEST_F(ListDirCacheTest, CreateFile) {
  std::vector<DirEntry> entries1, entries2;
  const string path = GetTestDirPath();
  FileStat file_stat(path);

  EXPECT_TRUE(Cache()->GetDirEntries(path, file_stat, &entries1));

  // Create a new file.
  CreateFileInTestDir("c");

  // No hit - file stat is updated.
  EXPECT_TRUE(Cache()->GetDirEntries(path, FileStat(path), &entries2));
  std::sort(entries2.begin(), entries2.end(), CompareDirEntry);

  // Both calls were misses.
  EXPECT_EQ(0, Cache()->hit());
  EXPECT_EQ(2, Cache()->miss());

  ASSERT_EQ(5, entries2.size());
  EXPECT_EQ(".", entries2[0].name);
  EXPECT_EQ("..", entries2[1].name);
  EXPECT_EQ("a", entries2[2].name);
  EXPECT_EQ("b", entries2[3].name);
  EXPECT_EQ("c", entries2[4].name);
}

}  // namespace devtools_goma
