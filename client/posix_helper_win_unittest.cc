// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "posix_helper_win.h"

#include "glog/logging.h"
#include "gtest/gtest.h"
#include "ioutil.h"
#include "mypath.h"
#include "path.h"

namespace devtools_goma {

TEST(PosixHelperWin, mkdtemp) {
  const char kTemplate[] = "abc_XXXXXX";
  string original = file::JoinPath(GetGomaTmpDir(), kTemplate);
  string to_change = original;
  CHECK(mkdtemp(&to_change[0]));
  EXPECT_NE(original, to_change);
  DeleteRecursivelyOrDie(to_change);
}

TEST(PosixHelperWin, mkdtemp_insufficient_Xs) {
  const char kTemplate[] = "abc_XXXXX";  // expect at least 6 Xs but 5.
  string original = file::JoinPath(GetGomaTmpDir(), kTemplate);
  string to_change = original;
  EXPECT_EQ(nullptr, mkdtemp(&to_change[0]));
  EXPECT_EQ(original, to_change);
  EXPECT_DEATH(DeleteRecursivelyOrDie(to_change), "");
}

TEST(PosixHelperWin, mkdtemp_no_Xs) {
  const char kTemplate[] = "abcdefg";
  string original = file::JoinPath(GetGomaTmpDir(), kTemplate);
  string to_change = original;
  EXPECT_EQ(nullptr, mkdtemp(&to_change[0]));
  EXPECT_EQ(original, to_change);
  EXPECT_DEATH(DeleteRecursivelyOrDie(to_change), "");
}

}  // namespace devtools_goma
