// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "file.h"

#ifndef _WIN32
# include <libgen.h>
# include <sys/stat.h>
# include <sys/types.h>
# include <unistd.h>
#else
# include "config_win.h"
#endif

#include <fstream>

#include "file_dir.h"
#include "glog/logging.h"
#include "path.h"

// TODO: Refactor code with Chromium base/file_util
// Chrome base has file_util.h that pretty much replaces most functions in this
// file.  I chose to do a quick patch here so that we can move on WIN32 build
// without being blocked by refactoring with Chrome base.

namespace File {

bool Copy(const char* from, const char* to, bool overwrite) {
#ifdef _WIN32
  if (!CopyFileA(from, to, !overwrite)) {
    DWORD err = GetLastError();
    LOG_SYSRESULT(err);
    LOG(WARNING) << "failed to copy file:"
                 << " from=" << from
                 << " to=" << to;
    return false;
  }

  return true;
#else
  std::ifstream ifs(from, std::ifstream::binary);
  if (!ifs) {
    LOG(WARNING) << "Input file not found: " << from;
    return false;
  }

  struct stat stat_buf;
  if (!overwrite &&
      (0 == stat(to, &stat_buf))) {
    LOG(ERROR) << "File " << to << " exists and overwrite is disabled";
    return false;
  }

  std::ofstream ofs(to, std::ofstream::binary);
  if (!ofs) {
    LOG(WARNING) << "Cannot open output file: " << to;
    return false;
  }

  bool ok = true;
  while (!ifs.eof()) {
    char buf[4096];
    ifs.read(buf, sizeof(buf));
    if (ifs.fail() && !ifs.eof()) {
      LOG(WARNING) << "Failed to read file from: " << from;
      ok = false;
      break;
    }
    ofs.write(buf, ifs.gcount());
  }

  return ok;
#endif
}

bool CreateDir(const std::string& path, int mode) {
#ifndef _WIN32
  int r = mkdir(path.c_str(), mode);
  if (r < 0) {
    PLOG(ERROR) << "CreateDir failed: " << path;
    return false;
  }
#else
  UNREFERENCED_PARAMETER(mode);
  if (!CreateDirectoryA(path.c_str(), nullptr)) {
    DWORD err = GetLastError();
    LOG(ERROR) << "CreateDir failed: " << path << ": " << err;
    LOG_SYSRESULT(err);
    return false;
  }
#endif
  return true;
}

bool IsDirectory(const char* path) {
#ifndef _WIN32
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return false;
#else
  DWORD attr = GetFileAttributesA(path);
  if (attr == INVALID_FILE_ATTRIBUTES)
    return false;
  return (attr & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
#endif
}

}  // namespace File
