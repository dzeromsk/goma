// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "scoped_tmp_file.h"

#include <limits.h>

#include <sstream>

#include "file.h"
#include "file_dir.h"
#include "glog/logging.h"
#include "mypath.h"
#include "path.h"
#include "scoped_fd.h"

#ifdef _WIN32
# include "posix_helper_win.h"
#endif

namespace devtools_goma {

ScopedTmpFile::ScopedTmpFile(const string& prefix) {
#ifndef _WIN32
  static const char kMkstempMarker[] = "XXXXXX";
  filename_.assign(file::JoinPath(GetGomaTmpDir(), prefix));
  filename_.append(kMkstempMarker);
  fd_.reset(mkstemp(&filename_[0]));
#else
  char temp_file[MAX_PATH] = {0};
  if (GetTempFileNameA(GetGomaTmpDir().c_str(),
                       prefix.c_str(), 0, temp_file) != 0) {
    filename_ = temp_file;
    fd_.reset(ScopedFd::Create(filename_, 0600));
  }
#endif
  if (!fd_.valid()) {
    PLOG(ERROR) << "failed to create temp file:"
                << " filename=" << filename_;
  }
}

ScopedTmpFile::ScopedTmpFile(const string& prefix, const string& extension) {
  DCHECK(!extension.empty());
  DCHECK_EQ(extension[0], '.');
  static const int kNumRetries = 5;
  for (int retry = 0; retry < kNumRetries; ++retry) {
    std::ostringstream ss;
    ss << prefix;
    ss << rand();
    ss << extension;
    filename_ = file::JoinPath(GetGomaTmpDir(), ss.str());
    fd_.reset(ScopedFd::CreateExclusive(filename_, 0600));
    if (fd_.valid())
      break;
    LOG(INFO) << "failed to make a unique file: " << filename_;
  }
  LOG_IF(ERROR, !fd_.valid()) << "Could not have a valid tmp file."
                              << " prefix=" << prefix
                              << " extension=" << extension;
}

ScopedTmpFile::~ScopedTmpFile() {
  Close();
  remove(filename_.c_str());
}

bool ScopedTmpFile::valid() const {
  return fd_.valid();
}

ssize_t ScopedTmpFile::Write(const void* ptr, size_t len) const {
  return fd_.Write(ptr, len);
}

bool ScopedTmpFile::Close() {
  return fd_.Close();
}

ScopedTmpDir::ScopedTmpDir(const string& prefix) {
  char tmpdir[PATH_MAX];
  CheckTempDirectory(GetGomaTmpDir());
  static const char kTmpdirTemplate[] = "%s/%s_XXXXXXXX";
  DCHECK_LT(prefix.size() + sizeof(kTmpdirTemplate),
            static_cast<size_t>(PATH_MAX));
#ifdef _WIN32
  sprintf_s(tmpdir, sizeof(tmpdir), kTmpdirTemplate,
            GetGomaTmpDir().c_str(), prefix.c_str());
#else
  snprintf(tmpdir, sizeof(tmpdir), kTmpdirTemplate,
           GetGomaTmpDir().c_str(), prefix.c_str());
#endif
  if (mkdtemp(tmpdir) == nullptr) {
    dirname_.clear();
  } else {
    dirname_ = tmpdir;
  }
}

ScopedTmpDir::~ScopedTmpDir() {
  if (!valid()) {
    return;
  }
  if (!RecursivelyDelete(dirname_)) {
    LOG(ERROR) << "Failed to delete temporary directory: " << dirname_;
  }
}

}  // namespace devtools_goma
