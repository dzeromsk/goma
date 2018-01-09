// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <cstdlib>
#include <set>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "basictypes.h"
#include "static_darray.h"

namespace devtools_goma {
#include "static_darray_test_array.h"

namespace {
const int kMaxAppendChars = 20;
}

class StaticDoubleArrayTest : public testing::Test {
 public:
  void SetUp() override {
    srand(static_cast<unsigned int>(time(nullptr)));
    for (const auto& word : kDArrayKeywords) {
      keywords_.insert(string(word));
    }
  }

 protected:
  void LookupWord(const string& word) {
    int value = kDArrayArray.Lookup(word);
    if (keywords_.find(word) == keywords_.end()) {
      EXPECT_EQ(-1, value);
    } else {
      ASSERT_LT(static_cast<size_t>(value), arraysize(kDArrayKeywords));
      EXPECT_EQ(word, string(kDArrayKeywords[value]));
    }
  }

  std::set<string> keywords_;
};

TEST_F(StaticDoubleArrayTest, Lookup) {
  for (std::set<string>::iterator iter = keywords_.begin();
       iter != keywords_.end();
       ++iter) {
    string keyword(*iter);
    LookupWord(keyword);

    if (keyword.length() > 1) {
      int random_substr_len = rand() % (keyword.length() - 1) + 1;
      LookupWord(keyword.substr(0, random_substr_len));
    }

    std::ostringstream ss;
    ss << keyword;
    int random_append_len = rand() % kMaxAppendChars;
    for (int i = 0; i < random_append_len; ++i) {
      ss << static_cast<char>(rand() % 127 + 1);
    }
    LookupWord(ss.str());
  }
}

}  // namespace devtools_goma
