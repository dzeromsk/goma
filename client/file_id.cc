// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "file_id.h"

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
#include "scoped_fd.h"

#ifdef _WIN32
namespace {

bool InitFromInfo(const BY_HANDLE_FILE_INFORMATION& info,
                  devtools_goma::FileId* file_id) {
  if (info.nFileSizeHigh != 0) {
    LOG(ERROR) << "Goma won't handle a file whose size is larger than 4 GB.";
    return false;
  }

  if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    file_id->is_directory = true;
  }

  file_id->size = static_cast<off_t>(info.nFileSizeLow);
  file_id->mtime =
      devtools_goma::ConvertFiletimeToUnixTime(info.ftLastWriteTime);
  file_id->volume_serial_number = info.dwVolumeSerialNumber;
  file_id->file_index_high = info.nFileIndexHigh;
  file_id->file_index_low = info.nFileIndexLow;
  return true;
}

}  // namespace
#endif

namespace devtools_goma {

const off_t FileId::kInvalidFileSize = -1;

FileId::FileId(const string& filename)
  :
#ifdef _WIN32
    volume_serial_number(0), file_index_high(0), file_index_low(0),
#else
    dev(0), inode(0),
#endif
    mtime(0), size(kInvalidFileSize),
    is_directory(false) {
  GOMA_COUNTERZ("FileId");
#ifndef _WIN32
  struct stat stat_buf;
  if (stat(filename.c_str(), &stat_buf) == 0) {
    InitFromStat(stat_buf);
  }
#else
  // See: https://msdn.microsoft.com/en-us/library/aa363788(v=vs.85).aspx
  BY_HANDLE_FILE_INFORMATION fileinfo;
  ScopedFd fd(ScopedFd::OpenForStat(filename));
  if (fd.valid() && GetFileInformationByHandle(fd.handle(), &fileinfo)) {
    if (!InitFromInfo(fileinfo, this)) {
      LOG(WARNING) << "Error in init file id."
                   << " filename=" << filename;
    }
  }
#endif
}

#ifndef _WIN32
void FileId::InitFromStat(const struct stat& stat_buf) {
  dev = stat_buf.st_dev;
  inode = stat_buf.st_ino;
  mtime = stat_buf.st_mtime;
  size = stat_buf.st_size;
  is_directory = S_ISDIR(stat_buf.st_mode);
}
#endif

bool FileId::IsValid() const {
  return size != kInvalidFileSize;
}

bool FileId::CanBeNewerThan(const FileId& old, time_t last_checked) const {
  // If mtime >= last_checked - 1, the file might be updated within
  // the same second. We need to re-check the file for this case, too.
  // The minus one is for VMs, where mtime can delay 1 second.
  return mtime >= last_checked - 1 || *this != old;
}

std::string FileId::DebugString() const {
  std::stringstream ss;
  ss << "{";
#ifdef _WIN32
  ss << "volume_serial_number=" << volume_serial_number;
  ss << " file_index_high=" << file_index_high;
  ss << " file_index_low=" << file_index_low;
#else
  ss << "dev=" << dev;
  ss << " inode=" << inode;
#endif

  ss << " mtime=" << mtime;
  ss << " size=" << size;
  ss << " is_directory=" << is_directory;
  ss << "}";
  return ss.str();
}

}  // namespace devtools_goma
