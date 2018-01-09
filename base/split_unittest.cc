// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "split.h"

#include <string>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

TEST(SplitTest, SplitStringUsing) {
  std::vector<string> tokens;

  SplitStringUsing("foo:bar:baz", ":", &tokens);
  ASSERT_EQ(3, static_cast<int>(tokens.size()));
  EXPECT_EQ("foo", tokens[0]);
  EXPECT_EQ("bar", tokens[1]);
  EXPECT_EQ("baz", tokens[2]);

  SplitStringUsing(":bar:baz", ":", &tokens);
  ASSERT_EQ(3, static_cast<int>(tokens.size()));
  EXPECT_EQ("", tokens[0]);
  EXPECT_EQ("bar", tokens[1]);
  EXPECT_EQ("baz", tokens[2]);

  SplitStringUsing("::", "::", &tokens);
  ASSERT_EQ(2, static_cast<int>(tokens.size()));
  EXPECT_EQ("", tokens[0]);
  EXPECT_EQ("", tokens[1]);

  SplitStringUsing("ab:cd;ef:", ":;", &tokens);
  ASSERT_EQ(4, static_cast<int>(tokens.size()));
  EXPECT_EQ("ab", tokens[0]);
  EXPECT_EQ("cd", tokens[1]);
  EXPECT_EQ("ef", tokens[2]);
  EXPECT_EQ("", tokens[3]);

  SplitStringUsing("ab:;cd;:ef:;", ":;", &tokens);
  ASSERT_EQ(4, static_cast<int>(tokens.size()));
  EXPECT_EQ("ab", tokens[0]);
  EXPECT_EQ("cd", tokens[1]);
  EXPECT_EQ("ef", tokens[2]);
  EXPECT_EQ("", tokens[3]);

  SplitStringUsing("foo", "::", &tokens);
  ASSERT_EQ(1, static_cast<int>(tokens.size()));
  EXPECT_EQ("foo", tokens[0]);
}

TEST(SplitTest, SplitStringWithNul) {
  static const char orig[] = {
    'f', 'o', 'o', '\0',
    'b', 'a', 'r', '\0', '\0',
    'b', 'a', 'z',
    '\0', '\0',
  };

  string s1(orig, sizeof(orig) - 2);
  ASSERT_EQ(12U, s1.size());
  string s2(orig, sizeof(orig) - 1);
  ASSERT_EQ(13U, s2.size());
  string s3(orig, sizeof(orig));
  ASSERT_EQ(14U, s3.size());

  std::vector<string> tokens = strings::Split(s1, '\0');
  ASSERT_EQ(3U, tokens.size());
  EXPECT_EQ("foo", tokens[0]);
  EXPECT_EQ("bar", tokens[1]);
  EXPECT_EQ("baz", tokens[2]);

  tokens = strings::Split(s2, '\0');
  ASSERT_EQ(4U, tokens.size());
  EXPECT_EQ("foo", tokens[0]);
  EXPECT_EQ("bar", tokens[1]);
  EXPECT_EQ("baz", tokens[2]);
  EXPECT_EQ("", tokens[3]);

  // The consequence delimiter is skipped.
  tokens = strings::Split(s3, '\0');
  ASSERT_EQ(4U, tokens.size());
  EXPECT_EQ("foo", tokens[0]);
  EXPECT_EQ("bar", tokens[1]);
  EXPECT_EQ("baz", tokens[2]);
  EXPECT_EQ("", tokens[3]);
}

TEST(SplitTest, IncludeProcessor) {
  std::vector<string> tokens;

  SplitStringUsing(
      " /usr/include/c++/4.2\n"
      " /usr/include/c++/4.2/x86_64-linux-gnu\n"
      " /usr/include/c++/4.2/backward\n"
      " /usr/local/include\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.2.4/include\n"
      " /usr/include\n", "\r\n ", &tokens);
  ASSERT_EQ(8, static_cast<int>(tokens.size()));
  EXPECT_EQ("", tokens[0]);
  EXPECT_EQ("/usr/include/c++/4.2", tokens[1]);
  EXPECT_EQ("/usr/include/c++/4.2/x86_64-linux-gnu", tokens[2]);
  EXPECT_EQ("/usr/include/c++/4.2/backward", tokens[3]);
  EXPECT_EQ("/usr/local/include", tokens[4]);
  EXPECT_EQ("/usr/lib/gcc/x86_64-linux-gnu/4.2.4/include", tokens[5]);
  EXPECT_EQ("/usr/include", tokens[6]);
  EXPECT_EQ("", tokens[7]);

  std::vector<string> new_tokens;
  for (const auto& it : tokens) {
    if (it != "") {
      new_tokens.push_back(it);
    }
  }

  ASSERT_EQ(6, static_cast<int>(new_tokens.size()));
  EXPECT_EQ("/usr/include/c++/4.2", new_tokens[0]);
  EXPECT_EQ("/usr/include/c++/4.2/x86_64-linux-gnu", new_tokens[1]);
  EXPECT_EQ("/usr/include/c++/4.2/backward", new_tokens[2]);
  EXPECT_EQ("/usr/local/include", new_tokens[3]);
  EXPECT_EQ("/usr/lib/gcc/x86_64-linux-gnu/4.2.4/include", new_tokens[4]);
  EXPECT_EQ("/usr/include", new_tokens[5]);
}
