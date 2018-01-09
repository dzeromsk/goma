// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "join.h"

#include <string>
#include <vector>

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

TEST(JoinTest, JoinStrings) {
  std::vector<string> tokens;
  tokens.push_back("foo");
  tokens.push_back("bar");
  tokens.push_back("baz");
  string result;
  JoinStrings(tokens, "::", &result);
  EXPECT_EQ("foo::bar::baz", result);
}
