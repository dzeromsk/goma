// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "posix_helper_win.h"

#include <cstring>
#include <iostream>
#include <string>

#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "path.h"
#include "rand_util.h"

namespace {

const int kMaxRetry = 5;

}  // namespace

namespace devtools_goma {

int access(const char* path, int amode) {
  DWORD attr = GetFileAttributesA(path);
  if ((amode & R_OK) == R_OK) {
    if (attr == INVALID_FILE_ATTRIBUTES ||
        attr & FILE_ATTRIBUTE_DIRECTORY) {
      return -1;
    }
  }
  if ((amode & W_OK) == W_OK) {
    if (attr & FILE_ATTRIBUTE_READONLY || attr & FILE_ATTRIBUTE_SYSTEM ||
        attr & FILE_ATTRIBUTE_HIDDEN || attr & FILE_ATTRIBUTE_DIRECTORY) {
      return -1;
    }
  }
  if ((amode & X_OK) == X_OK) {
    if (attr == INVALID_FILE_ATTRIBUTES ||
        attr & FILE_ATTRIBUTE_DIRECTORY) {
      return -1;
    }
    absl::string_view extension = file::Extension(path);
    // TODO: use PATHEXT env. instead.
    if (extension != "exe" && extension != "cmd" && extension != "bat") {
      return -1;
    }
  }
  if (amode == F_OK) {
    return (attr == INVALID_FILE_ATTRIBUTES) ? -1 : 0;
  }
  if ((amode & ~(R_OK|W_OK|X_OK|F_OK)) != 0) {
    LOG(ERROR) << "unknown access mask: 0" << std::oct << amode;
    return -1;
  }
  return 0;
}

char *mkdtemp(char *tmpl) {
  absl::string_view t(tmpl);
  absl::string_view::size_type pos = t.find_last_not_of('X');
  if (pos == absl::string_view::npos) {
    LOG(ERROR) << "mkdtemp template has no X:" << t;
    return nullptr;
  }
  ++pos;  // to point the beginning of Xs.
  size_t x_length = t.length() - pos;
  if (x_length < 6) {
    LOG(ERROR) << "mkdtemp template has fewer X=" << x_length
               << " in " << t;
    return nullptr;
  }

  const std::string prefix = std::string(t.substr(0, pos));
  for (int retry = 0; retry < kMaxRetry; ++retry) {
    std::string dirname = prefix + GetRandomAlphanumeric(x_length);
    if (CreateDirectoryA(dirname.c_str(), nullptr)) {
      std::memcpy(tmpl, dirname.c_str(), t.length());
      return tmpl;
    }
    LOG_SYSRESULT(GetLastError());
    LOG(WARNING) << "mkdtemp failed to mkdir " << dirname;
  }
  LOG(ERROR) << "mkdtemp failed to create unique dir " << t;
  return nullptr;
}

}  // namespace devtools_goma
