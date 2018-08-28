// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "file_stat.h"

#include <gtest/gtest.h>
#include <cstdio>

#include "absl/time/clock.h"
#include "scoped_tmp_file.h"

namespace devtools_goma {

namespace {

// The file timestamp might only have 1-second resolution, and might not be
// completely in sync with the source of absl::Now(). To avoid any flaky tests,
// allow for this much margin of error when comparing file timestamps against
// absl::Now(). The goal is to make sure that we are getting a file timestamp
// that is recent, but not necessarily down to sub-second precision.
constexpr absl::Duration kFileStatMtimeMarginOfError = absl::Seconds(2);

}  // namespace

TEST(FileStatTest, DefaultConstructor) {
  FileStat dummy_stat;

  EXPECT_FALSE(dummy_stat.IsValid());
  EXPECT_FALSE(dummy_stat.mtime.has_value());
}

TEST(FileStatTest, InitFromDirectory) {
  const absl::Time start_time = absl::Now();
  ScopedTmpDir dir("dir");

  FileStat dir_stat(dir.dirname());

  EXPECT_TRUE(dir_stat.IsValid());
  EXPECT_TRUE(dir_stat.is_directory);

  ASSERT_TRUE(dir_stat.mtime.has_value());
  EXPECT_GE(*dir_stat.mtime, start_time - kFileStatMtimeMarginOfError);
}

TEST(FileStatTest, InitFromEmptyFile) {
  const absl::Time start_time = absl::Now();
  ScopedTmpFile file("file");

  FileStat file_stat(file.filename());

  EXPECT_TRUE(file_stat.IsValid());
  EXPECT_EQ(0, file_stat.size);
  EXPECT_FALSE(file_stat.is_directory);

  EXPECT_TRUE(file_stat.mtime.has_value());
  EXPECT_GE(*file_stat.mtime, start_time - kFileStatMtimeMarginOfError);
}

TEST(FileStatTest, InitFromNonEmptyFile) {
  const absl::Time start_time = absl::Now();
  const std::string kContents = "The quick brown fox jumps over the lazy dog.";
  ScopedTmpFile file("file");
  file.Write(kContents.c_str(), kContents.size());

  FileStat file_stat(file.filename());

  EXPECT_TRUE(file_stat.IsValid());
  EXPECT_EQ(kContents.size(), file_stat.size);
  EXPECT_FALSE(file_stat.is_directory);

  EXPECT_TRUE(file_stat.mtime.has_value());
  EXPECT_GE(*file_stat.mtime, start_time - kFileStatMtimeMarginOfError);
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
  FileStat stat1, stat2, stat3;
  FileStat stat_notime1, stat_notime2;

  // The first three have valid timestamps.
  stat1.mtime = absl::FromTimeT(100);
  stat1.size = 0;

  stat2.mtime = absl::FromTimeT(200);
  stat2.size = 0;

  stat3.mtime = absl::FromTimeT(200);
  stat3.size = 0;

  // These do not have valid timestamps -- but fill in the timestamp value
  // before clearing it.
  stat_notime1.mtime = absl::FromTimeT(100);
  stat_notime1.mtime.reset();
  stat_notime1.size = 0;

  stat_notime2.mtime = absl::FromTimeT(200);
  stat_notime2.mtime.reset();
  stat_notime2.size = 0;

  EXPECT_NE(stat1, stat2);  // Different valid time values.
  EXPECT_EQ(stat2, stat3);  // Same valid time values.

  EXPECT_EQ(stat_notime1, stat_notime2);  // No time values set: should be same.

  // Empty time values should not match valid time values.
  EXPECT_NE(stat1, stat_notime1);
  EXPECT_NE(stat2, stat_notime2);
}

}  // namespace devtools_goma
