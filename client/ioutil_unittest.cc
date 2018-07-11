// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "ioutil.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "unittest_util.h"

using std::string;

namespace devtools_goma {

#if GTEST_HAS_DEATH_TEST
TEST(IoutilTest, WriteStringToFileOrDieCrash) {
#ifndef _WIN32
  string not_exists = "/tmp/you_may_not_have_this_dir/foo/bar/baz";
  EXPECT_DEATH(WriteStringToFileOrDie("fuga", not_exists, 0666),
               "No such file");
#else
  string not_exists = "K:\\tmp\\you_may_not_have_this_dir\\foo\\bar\\baz";
  EXPECT_DEATH(WriteStringToFileOrDie("fuga", not_exists, 0666), "");
#endif
}
#endif  // GTEST_HAS_DEATH_TEST

TEST(IoutilTest, StringRstrip) {
  EXPECT_EQ("abc", StringRstrip("abc"));
  EXPECT_EQ("", StringRstrip(""));
  EXPECT_EQ("abc", StringRstrip("abc\n"));
  EXPECT_EQ("abc", StringRstrip("abc\r\n"));
  EXPECT_EQ("abc", StringRstrip("abc\r"));
  EXPECT_EQ("abc", StringRstrip("abc \r\n"));
  EXPECT_EQ("abc", StringRstrip("abc \r\n\v\f"));
  EXPECT_EQ("ab c", StringRstrip("ab c\r\n"));
  EXPECT_EQ("ab\nc", StringRstrip("ab\nc\r\n"));
  EXPECT_EQ(" abc", StringRstrip(" abc\r\n"));
  EXPECT_EQ("", StringStrip("\r\n "));
}

TEST(IoutilTest, StringStrip) {
  EXPECT_EQ("abc", StringStrip("abc"));
  EXPECT_EQ("", StringStrip(""));
  EXPECT_EQ("abc", StringStrip("\nabc\n"));
  EXPECT_EQ("abc", StringStrip("\r\nabc\r\n"));
  EXPECT_EQ("abc", StringStrip("\rabc\r"));
  EXPECT_EQ("abc", StringStrip(" \r\n abc \r\n"));
  EXPECT_EQ("abc", StringStrip("\v\f \r\n abc \r\n\v\f"));
  EXPECT_EQ("ab c", StringStrip("\r\n ab c\r\n"));
  EXPECT_EQ("ab\nc", StringStrip("\r\n ab\nc\r\n"));
  EXPECT_EQ("", StringStrip("\r\n "));
}

}  // namespace devtools_goma
