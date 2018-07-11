// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "util.h"

#include <algorithm>
#include <iterator>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "absl/strings/str_split.h"
#include "unittest_util.h"

using std::string;

TEST(Util, GetEnvFromEnvIter) {
  using devtools_goma::GetEnvFromEnvIter;
  std::vector<string> envs;
  envs.push_back("PATH=/usr/bin");
  envs.push_back("pAtHeXt=.EXE");

  // Should return "" for unknown name.
  EXPECT_EQ(GetEnvFromEnvIter(envs.begin(), envs.end(), "not_exist", true),
            "");
  EXPECT_EQ(GetEnvFromEnvIter(envs.begin(), envs.end(), "not_exist", false),
            "");

  // Should return "" if case is different and ignore_case=false.
  EXPECT_EQ(GetEnvFromEnvIter(envs.begin(), envs.end(), "pAtH", false), "");
  EXPECT_EQ(GetEnvFromEnvIter(envs.begin(), envs.end(), "pathext", false), "");

  // Should return value if case is different and ignore_case=true.
  EXPECT_EQ(GetEnvFromEnvIter(envs.begin(), envs.end(), "pAtH", true),
            "/usr/bin");
  EXPECT_EQ(GetEnvFromEnvIter(envs.begin(), envs.end(), "pathext", true),
            ".EXE");
}

TEST(Util, ReplaceEnvInEnvIter) {
  using devtools_goma::ReplaceEnvInEnvIter;
  std::vector<string> envs;
  envs.push_back("dummy1=dummy");
  envs.push_back("PATH=/usr/bin");
  envs.push_back("dummy2=dummy");

  std::vector<string> expected_envs;

  // Should return false if env not found and envs should be kept as is.
  std::copy(envs.begin(), envs.end(), std::back_inserter(expected_envs));
  EXPECT_FALSE(ReplaceEnvInEnvIter(envs.begin(), envs.end(), "not_exist",
                                   "should not change"));
  EXPECT_EQ(expected_envs, envs);

  // Should return true if env is replaced.
  EXPECT_TRUE(ReplaceEnvInEnvIter(envs.begin(), envs.end(), "PATH", "/sbin"));
  expected_envs[1] = "PATH=/sbin";
  EXPECT_EQ(expected_envs, envs);

#ifdef _WIN32
  // Should not change the original env name.
  EXPECT_TRUE(ReplaceEnvInEnvIter(envs.begin(), envs.end(), "path",
                                  "c:\\"));
  expected_envs[1] = "PATH=c:\\";
  EXPECT_EQ(expected_envs, envs);
#endif
}

TEST(Util, GetEnvShouldReturnValueContainingNul) {
  const string& env = devtools_goma::GetEnv("PATH");
  EXPECT_EQ(string(env.c_str()), env);
}

TEST(Util, ToShortNodename) {
  std::vector<std::pair<string, string>> testcases = {
    {"slave123-m1", "slave123-m1"},
    {"build123-m1.golo.chromium.org", "build123-m1"},
    {"BUILD123-M1", "build123-m1"},
  };

  for (const auto& tc : testcases) {
    EXPECT_EQ(tc.second, devtools_goma::ToShortNodename(tc.first));
  }
}

TEST(Util, ToVector) {
  std::vector<string> vs = ToVector(absl::StrSplit("x:y:z", ':'));
  EXPECT_EQ((std::vector<string> { "x", "y", "z" }), vs);
}
