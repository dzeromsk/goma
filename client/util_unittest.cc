// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "util.h"

#include <algorithm>
#include <iterator>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "unittest_util.h"

using std::string;

namespace {

#ifdef _WIN32
string LocateExecutable(
    const char* cwd_in, const char* path_in, const char* pathext_in,
    const char* cmd_in) {
  string path;
  if (path_in == nullptr) {
    path = devtools_goma::GetEnv("PATH");
    CHECK(!path.empty());
  } else {
    path.assign(path_in);
  }

  string pathext;
  if (pathext_in == nullptr) {
    pathext = devtools_goma::GetEnv("PATHEXT");
    CHECK(!pathext.empty());
  } else {
    pathext.assign(pathext_in);
  }

  string exec_path;
  if (devtools_goma::GetRealExecutablePath(
      nullptr, cmd_in, cwd_in, path, pathext, &exec_path, nullptr, nullptr)) {
    return exec_path;
  }
  return "";
}
#endif

}  // namespace

TEST(Util, GetRealExecutablePath) {
  // TODO: write test for POSIX.
#ifdef _WIN32
  string located = LocateExecutable("", nullptr, nullptr, "cmd");
  EXPECT_GT(located.size(), 3UL);

  // Shouls accept command with an extension.
  located = LocateExecutable("", nullptr, nullptr, "cmd.exe");
  EXPECT_GT(located.size(), 7UL);

  // Should ignore case.
  located = LocateExecutable("", nullptr, nullptr, "cmd.ExE");
  EXPECT_GT(located.size(), 7UL);

  // Not existing file.
  located = LocateExecutable("", nullptr, nullptr, "shall_not_have_this_file");
  EXPECT_TRUE(located.empty());

  // Empty PATHEXT.  Default pathext is used. i.e. it should not be empty.
  located = LocateExecutable("", nullptr, "", "cmd");
  EXPECT_GT(located.size(), 3UL);

  // Strange PATHEXT.  Nothing should match.
  located = LocateExecutable("", nullptr, ".non_exist_pathext", "cmd");
  EXPECT_TRUE(located.empty());

  // Expected PATHEXT.
  located = LocateExecutable("", nullptr, ".exe", "cmd");
  EXPECT_GT(located.size(), 3UL);

  // Expected PATHEXT with upper case letters.
  located = LocateExecutable("", nullptr, ".EXE", "cmd");
  EXPECT_GT(located.size(), 3UL);

  // Unexpected PATHEXT.
  located = LocateExecutable("", nullptr, ".com", "cmd");
  EXPECT_TRUE(located.empty());

  // Extension is not listed in PATHEXT. Nothing should match.
  located = LocateExecutable("", nullptr, ".com", "cmd.exe");
  EXPECT_TRUE(located.empty());

  // Expected PATHEXT comes after unexpected PATHEXT.
  located = LocateExecutable("", nullptr, ".com;.exe", "cmd");
  EXPECT_GT(located.size(), 3UL);

  // Expected PATHEXT comes after unexpected PATHEXT (upper case letters).
  located = LocateExecutable("", nullptr, ".COM;.EXE", "cmd");
  EXPECT_GT(located.size(), 3UL);

  // Expected PATHEXT should be automatically added even if full-path given.
  string expected = located;
  string input = located.substr(0, located.length() - 4);
  EXPECT_FALSE(input.empty());
  located = LocateExecutable("", "", nullptr, input.c_str());
  EXPECT_EQ(expected, located);
#endif
  // TODO: revise this using TmpdirUtil.
}

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
