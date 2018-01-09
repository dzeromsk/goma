// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "path_util.h"

#include <cctype>

#include "string_piece.h"
#include "string_piece_utils.h"

namespace devtools_goma {

bool IsPosixAbsolutePath(absl::string_view path) {
  return !path.empty() && path[0] == '/';
}

bool IsWindowsAbsolutePath(absl::string_view path) {
  // UNC
  if (path.size() > 3 && path[0] == '\\' && path[1] == '\\' &&
      path.find('\\', 3) != absl::string_view::npos &&
      path.find('/', 3) == absl::string_view::npos) {
    return true;
  }

  // local path.
  if (path.size() > 2 && std::isalpha(path[0]) && path[1] == ':' &&
      (path[2] == '/' || path[2] == '\\')) {
    return true;
  }

  return false;
}

bool HasPrefixDir(absl::string_view path, absl::string_view prefix) {
#ifdef _WIN32
  return HasPrefixDirWithSep(path, prefix, '\\') ||
      HasPrefixDirWithSep(path, prefix, '/');
#else
  return HasPrefixDirWithSep(path, prefix, '/');
#endif
}

bool HasPrefixDirWithSep(absl::string_view path, absl::string_view prefix,
                         char pathsep) {
  // TODO: do we need to convert path before check on Win path?
  // 1. need to make both lower case?

  if (!strings::StartsWith(path, prefix)) {
    return false;
  }
  if (path.size() == prefix.size()) {
    return true;
  }

  return path[prefix.size()] == pathsep;
}

absl::string_view GetFileNameExtension(absl::string_view filename) {
  absl::string_view::size_type last_sep = filename.find_last_of("/\\");
  if (last_sep != absl::string_view::npos) {
    filename.remove_prefix(last_sep + 1);
  }

  absl::string_view::size_type last_dot = filename.rfind('.');
  // Note: .config file should not be path extension.
  if (last_dot == absl::string_view::npos || last_dot == 0U) {
    return absl::ClippedSubstr(filename, filename.size(), 0);
  }
  return absl::ClippedSubstr(filename, last_dot + 1);
}

}  // namespace devtools_goma
