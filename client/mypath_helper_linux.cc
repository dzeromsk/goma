// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mypath_helper.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "absl/strings/str_cat.h"
#include "glog/logging.h"

namespace devtools_goma {

string GetPlatformSpecificTempDirectory() {
  const uid_t uid = getuid();
  const string dir = absl::StrCat("/run/user/", uid);

  struct stat st;
  if (lstat(dir.c_str(), &st) != 0) {
    PLOG(INFO) << "lstat failed."
               << " dir=" << dir;
    return string();
  }
  if (!S_ISDIR(st.st_mode)) {
    LOG(WARNING) << "not a directory."
                 << " dir=" << dir;
    return string();
  }
  // Return empty string if the directory is not owned by the user.
  // (b/116622386)
  if (st.st_uid != uid) {
    LOG(WARNING) << "directory is not owned by the user."
                 << " dir=" << dir
                 << " st_uid=" << st.st_uid
                 << " getuid=" << uid;
    return string();
  }
  if ((st.st_mode & S_IRWXU) != S_IRWXU) {
    LOG(WARNING) << "directory is not read/write/executable by the user."
                 << " dir=" << dir
                 << " st_mode=" << st.st_mode;
    return string();
  }
  if ((st.st_mode & (S_IRWXG|S_IRWXO)) != 0) {
    LOG(WARNING) << "directory is open to group or others."
                 << " dir=" << dir
                 << " st_mode=" << st.st_mode;
    return string();
  }

  return dir;
}

}  // namespace devtools_goma
