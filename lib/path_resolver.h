// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_PATH_RESOLVER_H_
#define DEVTOOLS_GOMA_LIB_PATH_RESOLVER_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "base/basictypes.h"
using std::string;

namespace devtools_goma {

class PathResolver {
 public:
  enum PathSeparatorType {
    kPosixPathSep = '/',
    kWin32PathSep = '\\',
  };

  enum PathCaseType {
    kPreserveCase,
    kLowerCase,
  };

  PathResolver();
  ~PathResolver();

  // Convert path to platform specific format of running platform.
  static string PlatformConvert(const string& path);
  static void PlatformConvertToString(const string& path, string* OUTPUT);

  // Convert path to platform specific format specified by |path_type|.
  // |case_type| to specify how the path should be normalized.
  // Note that |path_type|==kPosixPathSep will convert \ to /, so user couldn't
  // use \ in the path.
  // TODO: fix this.
  static string PlatformConvert(const string& path,
                                PathSeparatorType sep_type,
                                PathCaseType case_type);
  static void PlatformConvertToString(const string& path,
                                      PathSeparatorType sep_type,
                                      PathCaseType case_type,
                                      string* OUTPUT);

  // Removes . and .. from |path|.
  static string ResolvePath(absl::string_view path);

  // Removes . and .. from |path|.
  // |sep_type| is used for notifying ResolvePath separator type.
  // If kPosixPathSep is given, ResolvePath uses '/' as a path separator.
  // If kWin32PathSep is given, ResolvePath uses '/' and '\\' as path
  // separators, and paths are joined with '\\'.
  static string ResolvePath(absl::string_view path, PathSeparatorType sep_type);

  // Returns relative path from cwd.
  // If path and cwd doesn't share any directory hierarchy, returns path as is,
  // instead of relative path.
  // If path is already relative, returns path as is.
  // Note that if cwd is not real path (i.e, it contains symbolic link),
  // relative path may point different file.
  static string WeakRelativePath(const string& path, const string& cwd);

  // Returns true if path is under system paths.
  bool IsSystemPath(const string& path) const;

  // Registers path as system path.
  void RegisterSystemPath(const string& path);

  static const char kPathSep;

 private:
  std::vector<string> system_paths_;

  DISALLOW_COPY_AND_ASSIGN(PathResolver);
};

};  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_PATH_RESOLVER_H_
