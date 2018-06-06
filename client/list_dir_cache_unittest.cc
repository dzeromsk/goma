// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "list_dir_cache.h"

#include <gtest/gtest.h>

#include "path.h"
#include "unittest_util.h"

namespace devtools_goma {

TEST(ListDirCache, test) {
  TmpdirUtil tmpdir("list_dir_cache");

  tmpdir.CreateEmptyFile(file::JoinPath("dir", "a"));
  tmpdir.CreateEmptyFile(file::JoinPath("dir", "b"));

  ListDirCache::Init(1024);

  const string dir = tmpdir.FullPath("dir");
  std::vector<DirEntry> entries;
  FileStat file_stat(dir);

  // Decrease mtime for cache hit in second attempt.
  file_stat.mtime -= 2;

  // no hit - first access
  EXPECT_TRUE(
      ListDirCache::instance()->GetDirEntries(dir, file_stat, &entries));
  EXPECT_EQ(0, ListDirCache::instance()->hit());
  EXPECT_EQ(1, ListDirCache::instance()->miss());

  auto dir_entry_compare = [](const DirEntry& a, const DirEntry& b) {
    return a.name < b.name;
  };

  std::sort(entries.begin(), entries.end(), dir_entry_compare);

  ASSERT_EQ(4, entries.size());
  EXPECT_EQ(".", entries[0].name);
  EXPECT_EQ("..", entries[1].name);
  EXPECT_EQ("a", entries[2].name);
  EXPECT_EQ("b", entries[3].name);

  // cache hit
  EXPECT_TRUE(
      ListDirCache::instance()->GetDirEntries(dir, file_stat, &entries));
  EXPECT_EQ(1, ListDirCache::instance()->hit());
  EXPECT_EQ(1, ListDirCache::instance()->miss());
  std::sort(entries.begin(), entries.end(), dir_entry_compare);

  ASSERT_EQ(4, entries.size());
  EXPECT_EQ(".", entries[0].name);
  EXPECT_EQ("..", entries[1].name);
  EXPECT_EQ("a", entries[2].name);
  EXPECT_EQ("b", entries[3].name);

  tmpdir.CreateEmptyFile(file::JoinPath("dir", "c"));

  // no hit - file stat is updated
  EXPECT_TRUE(
      ListDirCache::instance()->GetDirEntries(dir, FileStat(dir), &entries));
  EXPECT_EQ(1, ListDirCache::instance()->hit());
  EXPECT_EQ(2, ListDirCache::instance()->miss());
  std::sort(entries.begin(), entries.end(), dir_entry_compare);

  ASSERT_EQ(5, entries.size());
  EXPECT_EQ(".", entries[0].name);
  EXPECT_EQ("..", entries[1].name);
  EXPECT_EQ("a", entries[2].name);
  EXPECT_EQ("b", entries[3].name);
  EXPECT_EQ("c", entries[4].name);

  // no hit - directory does not exist
  EXPECT_FALSE(ListDirCache::instance()->GetDirEntries(
      "notexist", FileStat(tmpdir.FullPath("notexist")), &entries));
  EXPECT_EQ(1, ListDirCache::instance()->hit());
  EXPECT_EQ(3, ListDirCache::instance()->miss());

  ListDirCache::Quit();
}

}  // namespace devtools_goma
