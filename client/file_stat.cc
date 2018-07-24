// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "file_stat.h"

#include <sys/stat.h>
#include <sstream>

#ifndef _WIN32
#include <sys/types.h>
#include <unistd.h>
#else
#include "filetime_win.h"
#endif
#include "counterz.h"
#include "glog/logging.h"

#ifdef _WIN32
namespace {

bool InitFromInfo(const WIN32_FILE_ATTRIBUTE_DATA& info,
                  devtools_goma::FileStat* file_stat) {
  if (info.nFileSizeHigh != 0) {
    LOG(ERROR) << "Goma won't handle a file whose size is larger than 4 GB.";
    return false;
  }

  if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    file_stat->is_directory = true;
  }

  file_stat->size = static_cast<off_t>(info.nFileSizeLow);
  file_stat->mtime =
      absl::ToTimeT(
          devtools_goma::ConvertFiletimeToAbslTime(info.ftLastWriteTime));
  return true;
}

}  // namespace
#endif

namespace devtools_goma {

const off_t FileStat::kInvalidFileSize = -1;

FileStat::FileStat(const string& filename)
    : mtime(0), size(kInvalidFileSize), is_directory(false) {
  GOMA_COUNTERZ("FileStat");
#ifndef _WIN32
  struct stat stat_buf;
  if (stat(filename.c_str(), &stat_buf) == 0) {
    InitFromStat(stat_buf);
  }
#else
  WIN32_FILE_ATTRIBUTE_DATA fileinfo;
  if (GetFileAttributesExA(filename.c_str(), GetFileExInfoStandard,
                           &fileinfo)) {
    if (!InitFromInfo(fileinfo, this)) {
      LOG(WARNING) << "Error in init file id."
                   << " filename=" << filename;
    }
  }
#endif
}

#ifndef _WIN32
void FileStat::InitFromStat(const struct stat& stat_buf) {
  mtime = stat_buf.st_mtime;
  size = stat_buf.st_size;
  is_directory = S_ISDIR(stat_buf.st_mode);
}
#endif

bool FileStat::IsValid() const {
  return size != kInvalidFileSize;
}

bool FileStat::CanBeNewerThan(const FileStat& old, time_t last_checked) const {
  // If mtime >= last_checked - 1, the file might be updated within
  // the same second. We need to re-check the file for this case, too.
  // The minus one is for VMs, where mtime can delay 1 second.
  return mtime >= last_checked - 1 || *this != old;
}

std::string FileStat::DebugString() const {
  std::stringstream ss;
  ss << "{";
  ss << " mtime=" << mtime;
  ss << " size=" << size;
  ss << " is_directory=" << is_directory;
  ss << "}";
  return ss.str();
}

std::ostream& operator<<(std::ostream& os, const FileStat& stat) {
  return os << stat.DebugString();
}

}  // namespace devtools_goma
