// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_UNITTEST_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_UNITTEST_UTIL_H_

#include <string>

#include "absl/time/time.h"

using std::string;

namespace devtools_goma {

class TmpdirUtil {
 public:
  explicit TmpdirUtil(const string& id);
  virtual ~TmpdirUtil();
  // Note: avoid CreateFile not to see "CreateFileW not found" on Win.
  virtual void CreateTmpFile(const string& path, const string& data);
  virtual void CreateEmptyFile(const string& path);
  virtual void MkdirForPath(const string& path, bool is_dir);

  virtual void RemoveTmpFile(const string& path);

  const string& tmpdir() const { return tmpdir_; }
  const string& cwd() const { return cwd_; }
  string realcwd() const;
  void SetCwd(const string cwd) { cwd_ = cwd; }
  string FullPath(const string& path) const;

 private:
  string cwd_;
  string tmpdir_;
};

string GetTestFilePath(const string& test_name);

bool UpdateMtime(const string& path, absl::Time mtime);

// Takes clang path.
// If GOMATEST_CLANG_PATH is specified, it's preferred.
// Otherwise, we find clang from third_party.
//
// If failed to find clang, empty string is returned.
string GetClangPath();

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_UNITTEST_UTIL_H_
