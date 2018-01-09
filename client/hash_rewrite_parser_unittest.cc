// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hash_rewrite_parser.h"

#include <map>
#include <string>
#include <utility>

#include <gtest/gtest.h>

namespace devtools_goma {

TEST(ParseRewriteRuleTest, ShouldParseEmptyFile) {
  std::map<std::string,std::string> mapping;
  EXPECT_TRUE(ParseRewriteRule("", &mapping));
  EXPECT_TRUE(mapping.empty());
}

TEST(ParseRewriteRuleTest, ShouldParseEmptyLines) {
  std::map<std::string,std::string> mapping;
  EXPECT_TRUE(ParseRewriteRule("\n\n\n", &mapping));
  EXPECT_TRUE(mapping.empty());
}

TEST(ParseRewriteRuleTest, ShouldParseOnelineFile) {
  std::map<std::string,std::string> mapping;
  std::map<std::string,std::string> expected;
  ASSERT_TRUE(expected.insert(std::make_pair(
      "b5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f",
      "b8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678"
      )).second);
  EXPECT_TRUE(ParseRewriteRule(
      "b5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "b8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678",
      &mapping));
  EXPECT_FALSE(mapping.empty());
  EXPECT_EQ(expected, mapping);
}

TEST(ParseRewriteRuleTest, ShouldParseTwolineFile) {
  std::map<std::string,std::string> mapping;
  std::map<std::string,std::string> expected;
  ASSERT_TRUE(expected.insert(std::make_pair(
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f",
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678"
      )).second);
  ASSERT_TRUE(expected.insert(std::make_pair(
      "b5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f",
      "b8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678"
      )).second);
  EXPECT_TRUE(ParseRewriteRule(
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n"
      "b5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "b8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n",
      &mapping));
  EXPECT_FALSE(mapping.empty());
  EXPECT_EQ(expected, mapping);
}

TEST(ParseRewriteRuleTest, ShouldReturnFalseIfNoDelimiter) {
  std::map<std::string,std::string> mapping;
  EXPECT_FALSE(ParseRewriteRule(
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f",
      &mapping));
}

TEST(ParseRewriteRuleTest, ShouldBeErrorIfNotSha256) {
  std::map<std::string,std::string> mapping;
  // Too short or long.
  EXPECT_FALSE(ParseRewriteRule(
      "a:"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n",
      &mapping));
  EXPECT_FALSE(ParseRewriteRule(
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "a\n",
      &mapping));
  EXPECT_FALSE(ParseRewriteRule(
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678abc:\n"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n",
      &mapping));
  EXPECT_FALSE(ParseRewriteRule(
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678:\n"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678abc\n",
      &mapping));
  // not hexdeciaml.
  EXPECT_FALSE(ParseRewriteRule(
      "ghi3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n",
      &mapping));
  EXPECT_FALSE(ParseRewriteRule(
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678:\n"
      "g8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n",
      &mapping));
}

TEST(ParseRewriteRuleTest, ShouldBeErrorForDuplicatedSourceEntry) {
  std::map<std::string,std::string> mapping;
  EXPECT_FALSE(ParseRewriteRule(
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n"
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "b8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n",
      &mapping));
}

TEST(ParseRewriteRuleTest, ShouldAcceptDuplicatedDestEntry) {
  std::map<std::string,std::string> mapping;
  std::map<std::string,std::string> expected;
  ASSERT_TRUE(expected.insert(std::make_pair(
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f",
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678"
      )).second);
  ASSERT_TRUE(expected.insert(std::make_pair(
      "b5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f",
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678"
      )).second);
  EXPECT_TRUE(ParseRewriteRule(
      "a5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n"
      "b5a3dadbdcafc7902f9502de7f037ec95f6340de8aa0a6b4d9ee74a47379063f:"
      "a8a38778b7c56de92f5f14c185104285f62c0dec8aed6e2f552cc73a8e9ac678\n",
      &mapping));
  EXPECT_FALSE(mapping.empty());
  EXPECT_EQ(expected, mapping);
}

}  // namespace devtools_goma
