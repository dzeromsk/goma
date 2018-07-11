// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/filesystem.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "base/config_win.h"

using std::string;

// TODO: add test for non WIN32

#if defined(_WIN32)
TEST(FilesystemTest, RecursivelyDelete) {
  char tmp_dir[PATH_MAX], first_dir[PATH_MAX];
  EXPECT_NE(0, GetTempPathA(PATH_MAX, tmp_dir));
  if (tmp_dir[strlen(tmp_dir) - 1] == '\\') {
    tmp_dir[strlen(tmp_dir) - 1] = 0;
  }
  EXPECT_NE(-1, sprintf_s(first_dir, PATH_MAX, "%s\\filesystem_unittest_%d",
                          tmp_dir, GetCurrentProcessId()));
  EXPECT_EQ(TRUE, CreateDirectoryA(first_dir, nullptr));
  string second_dir = first_dir;
  second_dir += "\\foo";
  EXPECT_EQ(TRUE, CreateDirectoryA(second_dir.c_str(), nullptr));
  string file = second_dir;
  file += "\\something.txt";
  FILE* fp = nullptr;
  EXPECT_EQ(0, fopen_s(&fp, file.c_str(), "w"));
  EXPECT_TRUE(fp != nullptr);
  fputs("bar", fp);
  fflush(fp);
  fclose(fp);
  EXPECT_TRUE(file::RecursivelyDelete(first_dir, file::Defaults()).ok());
  EXPECT_FALSE(file::RecursivelyDelete(first_dir, file::Defaults()).ok());
}
#endif
