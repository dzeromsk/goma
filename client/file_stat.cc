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

#include "absl/time/clock.h"
#include "absl/time/time.h"
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
      devtools_goma::ConvertFiletimeToAbslTime(info.ftLastWriteTime);
  return true;
}

}  // namespace
#endif

namespace devtools_goma {

const off_t FileStat::kInvalidFileSize = -1;

FileStat::FileStat(const string& filename)
    : size(kInvalidFileSize), is_directory(false), taken_at(absl::Now()) {
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
#ifdef __MACH__
  mtime = absl::TimeFromTimespec(stat_buf.st_mtimespec);
#else
  mtime = absl::TimeFromTimespec(stat_buf.st_mtim);
#endif

  size = stat_buf.st_size;
  is_directory = S_ISDIR(stat_buf.st_mode);
}
#endif

bool FileStat::IsValid() const {
  return size != kInvalidFileSize && mtime.has_value();
}

bool FileStat::CanBeNewerThan(const FileStat& old) const {
  return old.CanBeStale() || *this != old;
}

std::string FileStat::DebugString() const {
  std::stringstream ss;
  ss << "{";
  ss << " mtime=" << (mtime.has_value() ? absl::ToTimeT(*mtime) : 0);
  ss << " size=" << size;
  ss << " is_directory=" << is_directory;
  ss << "}";
  return ss.str();
}

bool FileStat::CanBeStale() const {
  DCHECK(mtime.has_value());

  // If mtime + 1 >= taken_at, the file might be updated within
  // the same second. We need to re-check the file for this case, too.
  // The plus one is for VMs, where mtime can delay 1 second or Apple's HFS.
  // TODO: make time resolution configurable.
  return *mtime + absl::Seconds(1) >= taken_at;
}

std::ostream& operator<<(std::ostream& os, const FileStat& stat) {
  return os << stat.DebugString();
}

}  // namespace devtools_goma
