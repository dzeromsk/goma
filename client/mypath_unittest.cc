// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mypath.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "file_dir.h"
#include "filesystem.h"
#include "ioutil.h"
#include "path.h"
#include "path_resolver.h"
#include "util.h"
#include "unittest_util.h"

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

TEST(Util, GetMyPathname) {
  // Make sure that GetMyPathname is resolved.
  EXPECT_EQ(
      devtools_goma::PathResolver::ResolvePath(devtools_goma::GetMyPathname()),
      devtools_goma::GetMyPathname());
}

TEST(Util, GetMyDirectory) {
  // Make sure that GetMyDirectory is resolved.
  EXPECT_EQ(
      devtools_goma::PathResolver::ResolvePath(devtools_goma::GetMyDirectory()),
      devtools_goma::GetMyDirectory());
}

#if GTEST_HAS_DEATH_TEST

TEST(Util, CheckTempDiretoryNotDirectory) {
  const string& tmpdir = devtools_goma::GetGomaTmpDir();
  file::RecursivelyDelete(tmpdir, file::Defaults());
  CHECK(file::CreateDir(tmpdir.c_str(), file::CreationMode(0700)).ok())
      << tmpdir;
  const string& tmpdir_file =
      file::JoinPath(tmpdir, "tmpdir_is_not_dir");
  devtools_goma::WriteStringToFileOrDie("", tmpdir_file, 0700);
#ifndef _WIN32
// TODO: enable CheckTempDiretoryNotDirectory on win.
// EXPECT_DEATH doesn't work well on windows?
// it failed to capture fatal message, but got
// *** Check failure stack trace: ***.
  EXPECT_DEATH(devtools_goma::CheckTempDirectory(tmpdir_file),
               "private goma tmp dir is not dir");
#else
  EXPECT_DEATH(devtools_goma::CheckTempDirectory(tmpdir_file), "");
#endif
  file::RecursivelyDelete(tmpdir, file::Defaults());
}

#ifndef _WIN32

TEST(Util, CheckTempDiretoryBadPermission) {
  const string& tmpdir = devtools_goma::GetGomaTmpDir();
  file::RecursivelyDelete(tmpdir, file::Defaults());
  mode_t omask = umask(022);
  PCHECK(mkdir(tmpdir.c_str(), 0744) == 0) << tmpdir;
  umask(omask);
  EXPECT_DEATH(devtools_goma::CheckTempDirectory(tmpdir),
               "private goma tmp dir is not owned only by you.");
  file::RecursivelyDelete(tmpdir, file::Defaults());
}
#endif
#endif  // GTEST_HAS_DEATH_TEST

#ifndef _WIN32
TEST(Util, GetCurrentDirNameOrDie) {
  using devtools_goma::GetCurrentDirNameOrDie;
  // NOTE: '1' in setenv mean overwrite.

  std::unique_ptr<char, decltype(&free)> original_env_pwd(nullptr, free);
  std::unique_ptr<char, decltype(&free)> original_cwd(nullptr, free);
  {
    const char* pwd = getenv("PWD");
    if (pwd != nullptr) {
      original_env_pwd.reset(strdup(pwd));
    }

    // Assuming we can obtain the resolved absolute cwd.
    original_cwd.reset(getcwd(nullptr, 0));
    ASSERT_NE(original_cwd.get(), nullptr);
  }

  // When PWD is invalid place, it should not be used.
  {
    ASSERT_EQ(setenv("PWD", "/somewhere/invalid/place", 1), 0);
    std::string cwd = GetCurrentDirNameOrDie();
    EXPECT_NE("/somewhere/invalid/place", cwd);
    // should be the same as getcwd.
    EXPECT_EQ(original_cwd.get(), cwd);
  }

  // When PWD is /proc/self/cwd, it should not be used.
  // Since the meaning of /proc/self/cwd is different among gomacc and
  // compiler_proxy, we should not use /proc/self/cwd.
  {
    ASSERT_EQ(setenv("PWD", "/proc/self/cwd", 1), 0);
    std::string cwd = GetCurrentDirNameOrDie();
    EXPECT_NE("/proc/self/cwd", cwd);
    // should be the same as getcwd.
    EXPECT_EQ(original_cwd.get(), cwd);
  }

  {
    devtools_goma::TmpdirUtil tmpdir("ioutil_tmpdir");
    // TODO: TmpdirUtil does not make cwd. why?
    tmpdir.MkdirForPath(tmpdir.cwd(), true);

    // Make a symlink $tmpdir_cwd/cwd --> real cwd.
    std::string newpath = tmpdir.FullPath("cwd");
    ASSERT_EQ(symlink(original_cwd.get(), newpath.c_str()), 0)
        << "from=" << newpath << " to=" << original_cwd.get();
    ASSERT_NE(original_cwd.get(), newpath);

    // set PWD as new path. Then the new path should be taken.
    setenv("PWD", newpath.c_str(), 1);
    std::string cwd = GetCurrentDirNameOrDie();
    EXPECT_EQ(cwd, newpath);

    // Need to unlink symlink. Otherwise. TmpdirUtil will recursively delete
    // the current working directory. Awful (>x<).
    ASSERT_EQ(unlink(newpath.c_str()), 0);
  }

  // ----- tear down the test for the safe.
  if (original_env_pwd) {
    setenv("PWD", original_cwd.get(), 1);
  } else {
    unsetenv("PWD");
  }
}
#endif
