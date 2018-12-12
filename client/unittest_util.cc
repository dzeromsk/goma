// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "unittest_util.h"

#ifdef _WIN32
#include <shlobj.h>
#include <sys/utime.h>
#include "config_win.h"
#else
# include <sys/stat.h>
# include <sys/types.h>
#include <utime.h>
#endif
#include <cstdio>
#include <limits.h>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "file_helper.h"
#include "filesystem.h"
#include "mypath.h"
#include "path.h"

#ifdef _WIN32
# include "posix_helper_win.h"
#endif

using std::string;

namespace devtools_goma {

TmpdirUtil::TmpdirUtil(const string& id) : cwd_("/cwd") {
  char tmpdir[PATH_MAX];
  CheckTempDirectory(GetGomaTmpDir());
#ifndef _WIN32
  static const char kTmpdirTemplate[] = "/tmp/%s_XXXXXXXX";
  DCHECK_LT(id.size() + sizeof(kTmpdirTemplate),
            static_cast<size_t>(PATH_MAX));
  snprintf(tmpdir, sizeof(tmpdir), kTmpdirTemplate, id.c_str());
  PCHECK(mkdtemp(tmpdir) != nullptr) << tmpdir;
#else
  static const char kTmpdirTemplate[] = "%s\\%s_XXXXXXXX";
  DCHECK_LT(id.size() + sizeof(kTmpdirTemplate),
            static_cast<size_t>(PATH_MAX));
  sprintf_s(tmpdir, sizeof(tmpdir), kTmpdirTemplate,
            GetGomaTmpDir().c_str(), id.c_str());
  CHECK(mkdtemp(tmpdir)) << "failed to make" << tmpdir
                         << " error code=" << GetLastError();
#endif
  tmpdir_ = tmpdir;
}

TmpdirUtil::~TmpdirUtil() {
  EXPECT_TRUE(file::RecursivelyDelete(tmpdir_, file::Defaults()).ok());
}

string TmpdirUtil::realcwd() const {
  return file::JoinPath(tmpdir_, cwd_);
}

string TmpdirUtil::FullPath(const string& path) const {
  return file::JoinPath(tmpdir_, file::JoinPathRespectAbsolute(cwd_, path));
}

void TmpdirUtil::CreateTmpFile(const string& path, const string& data) {
  MkdirForPath(path, false);
  ASSERT_TRUE(WriteStringToFile(data, FullPath(path)));
}

void TmpdirUtil::CreateEmptyFile(const string& path) {
  CreateTmpFile(path, "");
}

void TmpdirUtil::RemoveTmpFile(const string& path) {
#ifndef _WIN32
  unlink(FullPath(path).c_str());
#else
  DeleteFileA(FullPath(path).c_str());
#endif
}

void TmpdirUtil::MkdirForPath(const string& path, bool is_dir) {
  string fullpath = FullPath(path);
#ifndef _WIN32
  size_t pos = tmpdir_.size();
  while (pos != string::npos) {
    pos = fullpath.find_first_of('/', pos + 1);
    if (pos != string::npos) {
      VLOG(1) << "dir:" << fullpath.substr(0, pos);
      if (access(fullpath.substr(0, pos).c_str(), R_OK) == 0)
        continue;
      PCHECK(mkdir(fullpath.substr(0, pos).c_str(), 0777) == 0)
          << pos << ": " << fullpath.substr(0, pos);
    }
  }
  if (is_dir) {
    VLOG(1) << "dir:" << fullpath;
    PCHECK(mkdir(fullpath.c_str(), 0777) == 0) << fullpath;
  }
#else
  string dirname;
  if (is_dir) {
    dirname = fullpath;
  } else {
    size_t last_slash = fullpath.rfind('\\');
    dirname = fullpath.substr(0, last_slash);
    if (file::IsDirectory(dirname, file::Defaults()).ok())
      return;
  }
  int result = SHCreateDirectoryExA(nullptr, dirname.c_str(), nullptr);
  EXPECT_EQ(ERROR_SUCCESS, result);
  DWORD attr = GetFileAttributesA(dirname.c_str());
  // TODO: revise after write patch to glog to support PLOG on Win.
  CHECK_NE(attr, INVALID_FILE_ATTRIBUTES)
      << dirname
      << " error code=" << GetLastError();
  CHECK(attr & FILE_ATTRIBUTE_DIRECTORY)
      << dirname
      << " attr=" << attr
      << " error code=" << GetLastError();
#endif
}

string GetTestFilePath(const string& test_name) {
  // This module is out/Release/ar_unittest (Linux & Mac),
  // build\Release\ar_unittest.exe (Windows msvs),
  // or out\Release\ar_unittest.exe (Windows ninja).
  // Test files should be stored under test directory.
  const string fullpath =
      file::JoinPath(GetMyDirectory(), "..", "..", "test", test_name);

  CHECK_EQ(access(fullpath.c_str(), R_OK), 0)
    << "Cannot read test file:"
    << " filename=" << fullpath;
  return fullpath;
}

bool UpdateMtime(const string& path, absl::Time mtime) {
#ifdef _WIN32
  using utimbuf = _utimbuf;
  const auto utime = _utime;
#endif

  utimbuf new_stat{
      /* actime */ 0,
      /* modtime */ absl::ToTimeT(mtime),
  };

  return utime(path.c_str(), &new_stat) == 0;
}

string GetClangPath() {
  // If GOMATEST_CLANG_PATH is specified, we prefer it.
  char* clang = getenv("GOMATEST_CLANG_PATH");
  if (clang != nullptr) {
    if (access(clang, X_OK) < 0) {
      LOG(ERROR)
          << "GOMATEST_CLANG_PATH is specified, but it's not executable.";
      return string();
    }
    return clang;
  }

#ifdef _WIN32
  const string clang_path = "clang-cl.exe";
#else
  const string clang_path = "clang";
#endif
  const string fullpath =
      file::JoinPath(GetMyDirectory(), "..", "..", "third_party", "llvm-build",
                     "Release+Asserts", "bin", clang_path);
  if (access(fullpath.c_str(), X_OK) < 0) {
    LOG(ERROR) << "clang is not an executable: clang=" << fullpath;
    return string();
  }

  return fullpath;
}

}  // namespace devtools_goma
