// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILE_ID_H_
#define DEVTOOLS_GOMA_CLIENT_FILE_ID_H_

#include <time.h>
#ifndef _WIN32
#include <sys/stat.h>
#else
#include "config_win.h"
#endif

#include <string>

using std::string;

namespace devtools_goma {

// A helper class to check if a file is updated.
//
// Note: please also update compiler_info_data protobuf.
// FileId is used for detecting update of compilers/subprograms.
struct FileId {
  static const off_t kInvalidFileSize;
  FileId() :
#ifdef _WIN32
      volume_serial_number(0), file_index_high(0), file_index_low(0),
#else
      dev(0), inode(0),
#endif
      mtime(0), size(kInvalidFileSize),
      is_directory(false) {}
  explicit FileId(const string& filename);

  bool IsValid() const;
  bool CanBeNewerThan(const FileId& old, time_t last_checked) const;

  std::string DebugString() const;

  bool operator==(const FileId& other) const {
    return
#ifdef _WIN32
        volume_serial_number == other.volume_serial_number &&
        file_index_high == other.file_index_high &&
        file_index_low == other.file_index_low &&
#else
        dev == other.dev && inode == other.inode &&
#endif
        mtime == other.mtime && size == other.size &&
        is_directory == other.is_directory;
  }

  bool operator!=(const FileId& other) const {
    return !(*this == other);
  }

#ifdef _WIN32
  DWORD volume_serial_number;

  // 64bit FileIndex is not guaranteed to be unique in ReFS file system
  // introduced with Windows Server 2012.
  DWORD file_index_high;
  DWORD file_index_low;
#else
  dev_t dev;
  ino_t inode;
#endif
  time_t mtime;
  off_t size;
  bool is_directory;

 private:
#ifndef _WIN32
  void InitFromStat(const struct stat& stat_buf);
#endif
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FILE_ID_H_
