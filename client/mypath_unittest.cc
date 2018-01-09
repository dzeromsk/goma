// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mypath.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "file.h"
#include "file_dir.h"
#include "ioutil.h"
#include "path.h"
#include "util.h"

TEST(Util, GetUsername) {
  const string& user = devtools_goma::GetUsername();
  // smoke test.
  EXPECT_FALSE(user.empty());
  EXPECT_NE(user, "root");
  EXPECT_NE(user, "unknown");
}

TEST(Util, GetUsernameWithoutEnv) {
  devtools_goma::SetEnv("SUDO_USER", "");
  devtools_goma::SetEnv("USERNAME", "");
  devtools_goma::SetEnv("USER", "");
  devtools_goma::SetEnv("LOGNAME", "");

  EXPECT_EQ(devtools_goma::GetEnv("USER"), "");

  EXPECT_TRUE(devtools_goma::GetUsernameEnv().empty());
  const string username = devtools_goma::GetUsernameNoEnv();
  EXPECT_FALSE(username.empty());
  EXPECT_NE(username, "root");
  EXPECT_NE(username, "unknown");
  EXPECT_EQ(username, devtools_goma::GetUsername());
  EXPECT_EQ(username, devtools_goma::GetUsernameEnv());
}

#ifndef _WIN32
// TODO: enable CheckTempDiretoryNotDirectory on win.
// EXPECT_DEATH doesn't work well on windows?
// it failed to capture fatal message, but got
// *** Check failure stack trace: ***.
TEST(Util, CheckTempDiretoryNotDirectory) {
  const string& tmpdir = devtools_goma::GetGomaTmpDir();
  devtools_goma::RecursivelyDelete(tmpdir);
  CHECK(File::CreateDir(tmpdir.c_str(), 0700)) << tmpdir;
  const string& tmpdir_file =
      file::JoinPath(tmpdir, "tmpdir_is_not_dir");
  devtools_goma::WriteStringToFileOrDie("", tmpdir_file, 0700);
  EXPECT_DEATH(devtools_goma::CheckTempDirectory(tmpdir_file),
               "private goma tmp dir is not dir");
  devtools_goma::RecursivelyDelete(tmpdir);
}

TEST(Util, CheckTempDiretoryBadPermission) {
  const string& tmpdir = devtools_goma::GetGomaTmpDir();
  devtools_goma::RecursivelyDelete(tmpdir);
  mode_t omask = umask(022);
  PCHECK(mkdir(tmpdir.c_str(), 0744) == 0) << tmpdir;
  umask(omask);
  EXPECT_DEATH(devtools_goma::CheckTempDirectory(tmpdir),
               "private goma tmp dir is not owned only by you.");
  devtools_goma::RecursivelyDelete(tmpdir);
}

#endif
