// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "string_piece_utils.h"

#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

TEST(StringPieceUtils, StartsWith) {
  const std::string s1("123" "\0" "456", 7);
  const StringPiece a("foobar");
  const StringPiece b(s1);
  const StringPiece e;
  EXPECT_TRUE(strings::StartsWith(a, a));
  EXPECT_TRUE(strings::StartsWith(a, "foo"));
  EXPECT_TRUE(strings::StartsWith(a, e));
  EXPECT_TRUE(strings::StartsWith(b, s1));
  EXPECT_TRUE(strings::StartsWith(b, b));
  EXPECT_TRUE(strings::StartsWith(b, e));
  EXPECT_TRUE(strings::StartsWith(e, ""));
  EXPECT_FALSE(strings::StartsWith(a, b));
  EXPECT_FALSE(strings::StartsWith(b, a));
  EXPECT_FALSE(strings::StartsWith(e, a));

  // Same tests with HasPrefixString.
  EXPECT_TRUE(strings::StartsWith("foo/bar", "foo"));
  EXPECT_FALSE(strings::StartsWith("foo/bar", "bar"));
  EXPECT_TRUE(strings::StartsWith("foo/bar", "foo/bar"));
  EXPECT_FALSE(strings::StartsWith("foo/bar", "foo/bar/"));

  StringPiece abc("abcdefghijklmnopqrstuvwxyz");
  EXPECT_TRUE(strings::StartsWith(abc, abc));
  EXPECT_TRUE(strings::StartsWith(abc, "abcdefghijklm"));
  EXPECT_FALSE(strings::StartsWith(abc, "abcdefguvwxyz"));
}

TEST(StringPieceUtils, EndsWith) {
  const std::string s1("123" "\0" "456", 7);
  const StringPiece a("foobar");
  const StringPiece b(s1);
  const StringPiece e;
  EXPECT_TRUE(strings::EndsWith(a, a));
  EXPECT_TRUE(strings::EndsWith(a, "bar"));
  EXPECT_TRUE(strings::EndsWith(a, e));
  EXPECT_TRUE(strings::EndsWith(b, s1));
  EXPECT_TRUE(strings::EndsWith(b, b));
  EXPECT_TRUE(strings::EndsWith(b, e));
  EXPECT_TRUE(strings::EndsWith(e, ""));
  EXPECT_FALSE(strings::EndsWith(a, b));
  EXPECT_FALSE(strings::EndsWith(b, a));
  EXPECT_FALSE(strings::EndsWith(e, a));

  // Same tests with HasSuffixString.
  EXPECT_FALSE(strings::EndsWith("foo/bar", "foo"));
  EXPECT_TRUE(strings::EndsWith("foo/bar", "bar"));
  EXPECT_TRUE(strings::EndsWith("foo/bar", "foo/bar"));
  EXPECT_FALSE(strings::EndsWith("foo/bar", "foo/bar/"));

  StringPiece abc("abcdefghijklmnopqrstuvwxyz");
  EXPECT_TRUE(strings::EndsWith(abc, abc));
  EXPECT_FALSE(strings::EndsWith(abc, "abcdefguvwxyz"));
  EXPECT_TRUE(strings::EndsWith(abc, "nopqrstuvwxyz"));
}

TEST(StringPieceUtils, StrCat) {
  EXPECT_EQ("", strings::StrCat());
  EXPECT_EQ("a", strings::StrCat("a"));
  EXPECT_EQ("ab", strings::StrCat("a", "b"));
  EXPECT_EQ("abab", strings::StrCat("a", "b", "ab"));
}
