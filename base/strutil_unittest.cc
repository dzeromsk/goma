// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "strutil.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

TEST(StrUtilTest, StringReplace) {
  LOG(INFO) << "Testing StringReplace";
  string result2;

  // test StringReplace core functionality
  string value = "<attribute name=abcd/>";
  string sub = "=";
  string newsub = " = ";
  string expected_result = "<attribute name = abcd/>";
  string result = StringReplace(value, sub, newsub, 0);
  CHECK_EQ(expected_result, result);

  result2.clear();
  StringReplace(value, sub, newsub, 0, &result2);
  CHECK_EQ(expected_result, result2);

  // test for negative case
  value = "<attribute name=abcd/>";
  sub = "-";
  newsub = "=";
  expected_result = "<attribute name=abcd/>";
  result = StringReplace(value, sub, newsub, 0);
  CHECK_EQ(expected_result, result);

  result2.clear();
  StringReplace(value, sub, newsub, 0, &result2);
  CHECK_EQ(expected_result, result2);

  // test StringReplace core functionality with repeated flag set
  value = "<attribute name==abcd/>";
  sub = "=";
  newsub = " = ";
  expected_result = "<attribute name =  = abcd/>";
  result = StringReplace(value, sub, newsub, 1);
  CHECK_EQ(expected_result, result);

  result2.clear();
  StringReplace(value, sub, newsub, 1, &result2);
  CHECK_EQ(expected_result, result2);

  // input is an empty string
  value = "";
  sub = "=";
  newsub = " = ";
  expected_result = "";
  result = StringReplace(value, sub, newsub, 0);
  CHECK_EQ(expected_result, result);

  result2.clear();
  StringReplace(value, sub, newsub, 0, &result2);
  CHECK_EQ(expected_result, result2);

  // input is an empty string and this is a request for repeated
  // string replaces.
  value = "";
  sub = "=";
  newsub = " = ";
  expected_result = "";
  result = StringReplace(value, sub, newsub, 1);
  CHECK_EQ(expected_result, result);

  result2.clear();
  StringReplace(value, sub, newsub, 1, &result2);
  CHECK_EQ(expected_result, result2);

  // input and string to replace is an empty string.
  value = "";
  sub = "";
  newsub = " = ";
  expected_result = "";
  result = StringReplace(value, sub, newsub, 0);
  CHECK_EQ(expected_result, result);

  result2.clear();
  StringReplace(value, sub, newsub, 0, &result2);
  CHECK_EQ(expected_result, result2);
}
