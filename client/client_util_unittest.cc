// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client_util.h"

#include "glog/logging.h"
#include "gtest/gtest.h"
#include "unittest_util.h"
#include "util.h"

namespace devtools_goma {

namespace {

#ifdef _WIN32
string LocateExecutable(const char* cwd_in,
                        const char* path_in,
                        const char* pathext_in,
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
  if (devtools_goma::GetRealExecutablePath(nullptr, cmd_in, cwd_in, path,
                                           pathext, &exec_path, nullptr,
                                           nullptr)) {
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

}  // namespace devtools_goma
