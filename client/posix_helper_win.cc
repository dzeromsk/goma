// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "posix_helper_win.h"

#include <cstring>
#include <string>

#include "absl/strings/string_view.h"
#include "path.h"
#include "rand_util.h"

namespace {

const int kMaxRetry = 5;

}  // namespace

namespace devtools_goma {

int access(const char* path, int amode) {
  DWORD attr = GetFileAttributesA(path);
  switch (amode) {
    case R_OK:
      {
        if (attr == INVALID_FILE_ATTRIBUTES ||
            attr & FILE_ATTRIBUTE_DIRECTORY) {
          return -1;
        }
        return 0;
      }
    case W_OK:
      {
        if (attr & FILE_ATTRIBUTE_READONLY || attr & FILE_ATTRIBUTE_SYSTEM ||
            attr & FILE_ATTRIBUTE_HIDDEN || attr & FILE_ATTRIBUTE_DIRECTORY) {
          return -1;
        }
        return 0;
      }
    case X_OK:
      {
        if (attr == INVALID_FILE_ATTRIBUTES ||
            attr & FILE_ATTRIBUTE_DIRECTORY) {
          return -1;
        }
        absl::string_view extension = file::Extension(path);
        // TODO: use PATHEXT env. instead.
        if (extension == "exe" || extension == "cmd" || extension == "bat") {
          return 0;
        }
        return -1;
      }
    case F_OK:
      return (attr == INVALID_FILE_ATTRIBUTES) ? -1 : 0;
    default:
      return -1;
  }
}

char *mkdtemp(char *tmpl) {
  absl::string_view t(tmpl);
  absl::string_view::size_type pos = t.find_last_not_of('X');
  if (pos == absl::string_view::npos) {
    return nullptr;
  }
  ++pos;  // to point the beginning of Xs.
  size_t x_length = t.length() - pos;
  if (x_length < 6) {
    return nullptr;
  }

  const std::string prefix = std::string(t.substr(0, pos));
  for (int retry = 0; retry < kMaxRetry; ++retry) {
    std::string dirname = prefix + GetRandomAlphanumeric(x_length);
    if (CreateDirectoryA(dirname.c_str(), nullptr)) {
      std::memcpy(tmpl, dirname.c_str(), t.length());
      return tmpl;
    }
  }
  return nullptr;
}

}  // namespace devtools_goma
