// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILE_STAT_H_
#define DEVTOOLS_GOMA_CLIENT_FILE_STAT_H_

#include <time.h>
#ifndef _WIN32
#include <sys/stat.h>
#else
#include "config_win.h"
#endif

#include <ostream>
#include <string>

using std::string;

namespace devtools_goma {

// A helper class to check if a file is updated.
//
// Note: please also update compiler_info_data protobuf.
// FileStat is used for detecting update of compilers/subprograms.
struct FileStat {
  static const off_t kInvalidFileSize;
  FileStat() : mtime(0), size(kInvalidFileSize), is_directory(false) {}
  explicit FileStat(const string& filename);

  bool IsValid() const;
  bool CanBeNewerThan(const FileStat& old, time_t last_checked) const;

  std::string DebugString() const;

  bool operator==(const FileStat& other) const {
    return mtime == other.mtime && size == other.size &&
           is_directory == other.is_directory;
  }

  bool operator!=(const FileStat& other) const { return !(*this == other); }

  // For output during testing.
  friend std::ostream& operator<<(std::ostream& os, const FileStat& stat);

  time_t mtime;
  off_t size;
  bool is_directory;

 private:
#ifndef _WIN32
  void InitFromStat(const struct stat& stat_buf);
#endif
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FILE_STAT_H_
