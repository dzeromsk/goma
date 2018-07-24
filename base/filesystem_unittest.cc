// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/filesystem.h"

#include <fstream>
#include <memory>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "base/config_win.h"
#include "base/path.h"
#include "gtest/gtest.h"

using std::string;

// TODO: add test for non WIN32

namespace {

// Creates a unique tmp dir. Should be removed by yourself.
string CreateUniqueTmpDir() {
#ifdef _WIN32
  char tmp_dir[PATH_MAX], first_dir[PATH_MAX];
  EXPECT_NE(0, GetTempPathA(PATH_MAX, tmp_dir));
  if (tmp_dir[strlen(tmp_dir) - 1] == '\\') {
    tmp_dir[strlen(tmp_dir) - 1] = 0;
  }
  EXPECT_NE(-1, sprintf_s(first_dir, PATH_MAX, "%s\\filesystem_unittest_%d",
                          tmp_dir, GetCurrentProcessId()));
  EXPECT_EQ(TRUE, CreateDirectoryA(first_dir, nullptr));

  return first_dir;
#else
  char tmpdir[] = "/tmp/filesystem_unittest.XXXXXX";
  char* dir_name = mkdtemp(tmpdir);
  EXPECT_NE(nullptr, dir_name);

  return dir_name;
#endif
}

}  // anonymous namespace

#if defined(_WIN32)
TEST(FilesystemTest, RecursivelyDelete) {
  string first_dir = CreateUniqueTmpDir();
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

TEST(FilesystemTest, Copy) {
  string tmpdir = CreateUniqueTmpDir();

  string src = file::JoinPath(tmpdir, "src.txt");
  string dst = file::JoinPath(tmpdir, "dst.txt");
  {
    std::ofstream fs(src);
    fs << "ABC";
    EXPECT_TRUE(fs.good());
  }

  EXPECT_TRUE(file::Copy(src, dst, file::Defaults()).ok());
  {
    std::ifstream fs(dst);
    string s;
    fs >> s;
    EXPECT_EQ("ABC", s);
  }

  // cannot overwrite.
  EXPECT_FALSE(file::Copy(src, dst, file::Defaults()).ok());
  // can overwrite.
  EXPECT_TRUE(file::Copy(src, dst, file::Overwrite()).ok());

  EXPECT_TRUE(file::RecursivelyDelete(tmpdir, file::Defaults()).ok());
}
