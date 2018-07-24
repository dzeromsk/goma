// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_stat.h"

#include <gtest/gtest.h>
#include <cstdio>

#include "scoped_tmp_file.h"
//#include "unittest_util.h"

namespace devtools_goma {

TEST(FileStatTest, DefaultConstructor) {
  FileStat dummy_stat;

  EXPECT_FALSE(dummy_stat.IsValid());
}

TEST(FileStatTest, InitFromDirectory) {
  ScopedTmpDir dir("dir");

  FileStat dir_stat(dir.dirname());

  EXPECT_TRUE(dir_stat.IsValid());
  EXPECT_GT(dir_stat.mtime, 0);
  EXPECT_TRUE(dir_stat.is_directory);
}

TEST(FileStatTest, InitFromEmptyFile) {
  ScopedTmpFile file("file");

  FileStat file_stat(file.filename());

  EXPECT_TRUE(file_stat.IsValid());
  EXPECT_GT(file_stat.mtime, 0);
  EXPECT_EQ(0, file_stat.size);
  EXPECT_FALSE(file_stat.is_directory);
}

TEST(FileStatTest, InitFromNonEmptyFile) {
  ScopedTmpFile file("file");
  const std::string kContents = "The quick brown fox jumps over the lazy dog.";
  file.Write(kContents.c_str(), kContents.size());

  FileStat file_stat(file.filename());

  EXPECT_TRUE(file_stat.IsValid());
  EXPECT_GT(file_stat.mtime, 0);
  EXPECT_EQ(kContents.size(), file_stat.size);
  EXPECT_FALSE(file_stat.is_directory);
}

TEST(FileStatTest, ValidVersusInvalid) {
  ScopedTmpFile file("file");

  FileStat valid(file.filename());
  FileStat invalid;

  EXPECT_NE(valid, invalid);
}

TEST(FileStatTest, SameFile) {
  ScopedTmpFile file("file");

  FileStat file_stat1(file.filename());
  FileStat file_stat2(file.filename());

  EXPECT_EQ(file_stat1, file_stat2);
}

TEST(FileStatTest, DifferentTime) {
  // Instead of trying to create different files, manually fill these out.
  FileStat stat1, stat2;

  stat1.mtime = 100;
  stat1.size = 0;
  stat2.mtime = 200;
  stat2.size = 0;

  EXPECT_NE(stat1, stat2);
}

}  // namespace devtools_goma
