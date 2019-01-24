// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILE_STAT_H_
#define DEVTOOLS_GOMA_CLIENT_FILE_STAT_H_

#ifndef _WIN32
#include <sys/stat.h>
#else
#include "config_win.h"
#endif

#include <ostream>
#include <string>

#include "absl/time/time.h"
#include "absl/types/optional.h"

using std::string;

namespace devtools_goma {

// A helper class to check if a file is updated.
//
// Note: please also update compiler_info_data protobuf.
// FileStat is used for detecting update of compilers/subprograms.
struct FileStat {
  static const off_t kInvalidFileSize;
  FileStat() : size(kInvalidFileSize), is_directory(false) {}
  explicit FileStat(const string& filename);

  bool IsValid() const;
  bool CanBeNewerThan(const FileStat& old) const;

  std::string DebugString() const;

  // checks stat equals with other.
  //
  // Caution: use this for the same filename only. i.e. use only to
  // detect file is modified or not.
  // for different filenames, FileStat might be considered as equal
  // even if file differs, because it only checks mtime/stat/is_dir.
  bool operator==(const FileStat& other) const {
    return mtime == other.mtime && size == other.size &&
           is_directory == other.is_directory;
  }

  bool operator!=(const FileStat& other) const { return !(*this == other); }

  // Check whether filestat can be stale or not.
  // If a file is modified just after FileStat is taken, there is a case that
  // mtime is the same even though a file is changed (especially if mtime
  // resolution is coarse).
  // We say a filestat can be stale if mtime and the time when we take FileStat
  // are close enough.
  // It can be OK to use a stale FileStat in a compile unit (since it means a
  // file is changed during a compile), however, don't cache it.
  bool CanBeStale() const;

  // For output during testing.
  friend std::ostream& operator<<(std::ostream& os, const FileStat& stat);

  absl::optional<absl::Time> mtime;
  off_t size;
  bool is_directory;

 private:
#ifndef _WIN32
  void InitFromStat(const struct stat& stat_buf);
#endif

  // This is member to detect stale file stat.
  // This should be earlier than actual time when timestat is taken.
  absl::Time taken_at;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FILE_STAT_H_
