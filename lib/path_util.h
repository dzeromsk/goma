// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_PATH_UTIL_H_
#define DEVTOOLS_GOMA_LIB_PATH_UTIL_H_


#include "absl/strings/string_view.h"

namespace devtools_goma {

bool IsPosixAbsolutePath(absl::string_view path);
bool IsWindowsAbsolutePath(absl::string_view path);

bool HasPrefixDir(absl::string_view path, absl::string_view prefix);
bool HasPrefixDirWithSep(absl::string_view path, absl::string_view prefix,
                         char pathsep);

// Get file extension of the given |filename|.
// This function think both '/' and '\\' as path separators.
absl::string_view GetFileNameExtension(absl::string_view filename);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_PATH_UTIL_H_
