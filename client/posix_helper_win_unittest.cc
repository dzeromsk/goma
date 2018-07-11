// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "posix_helper_win.h"

#include "filesystem.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "mypath.h"
#include "path.h"
#include "status.h"

namespace devtools_goma {

TEST(PosixHelperWin, mkdtemp) {
  const char kTemplate[] = "abc_XXXXXX";
  string original = file::JoinPath(GetGomaTmpDir(), kTemplate);
  if (!file::IsDirectory(file::Dirname(original), file::Defaults()).ok()) {
    EXPECT_TRUE(file::CreateDir(file::Dirname(original),
                                file::CreationMode(0755)).ok());
  }
  string to_change = original;
  EXPECT_NE(nullptr, mkdtemp(&to_change[0])) << to_change;
  EXPECT_NE(original, to_change);
  EXPECT_TRUE(file::IsDirectory(to_change, file::Defaults()).ok())
      << to_change;
  ::util::Status status = file::RecursivelyDelete(to_change, file::Defaults());
  EXPECT_TRUE(status.ok()) << to_change;
}

TEST(PosixHelperWin, mkdtemp_insufficient_Xs) {
  const char kTemplate[] = "abc_XXXXX";  // expect at least 6 Xs but 5.
  string original = file::JoinPath(GetGomaTmpDir(), kTemplate);
  if (!file::IsDirectory(file::Dirname(original), file::Defaults()).ok()) {
    EXPECT_TRUE(file::CreateDir(file::Dirname(original),
                                file::CreationMode(0755)).ok());
  }
  string to_change = original;
  EXPECT_EQ(nullptr, mkdtemp(&to_change[0])) << to_change;
  EXPECT_EQ(original, to_change);
  EXPECT_FALSE(file::IsDirectory(to_change, file::Defaults()).ok())
      << to_change;
  ::util::Status status = file::RecursivelyDelete(to_change, file::Defaults());
  EXPECT_FALSE(status.ok()) << to_change;
}

TEST(PosixHelperWin, mkdtemp_no_Xs) {
  const char kTemplate[] = "abcdefg";
  string original = file::JoinPath(GetGomaTmpDir(), kTemplate);
  if (!file::IsDirectory(file::Dirname(original), file::Defaults()).ok()) {
    EXPECT_TRUE(file::CreateDir(file::Dirname(original),
                                file::CreationMode(0755)).ok());
  }
  string to_change = original;
  EXPECT_EQ(nullptr, mkdtemp(&to_change[0])) << to_change;
  EXPECT_EQ(original, to_change);
  EXPECT_FALSE(file::IsDirectory(to_change, file::Defaults()).ok())
      << to_change;
  ::util::Status status = file::RecursivelyDelete(to_change, file::Defaults());
  EXPECT_FALSE(status.ok()) << to_change;
}

}  // namespace devtools_goma
