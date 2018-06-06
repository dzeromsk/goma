// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mypath_helper.h"

#include <unistd.h>
#include <sys/types.h>

#include "absl/strings/str_cat.h"
#include "filesystem.h"

namespace devtools_goma {

string GetPlatformSpecificTempDirectory() {
  string dir = absl::StrCat("/run/user/", getuid());
  if (file::IsDirectory(dir, file::Defaults()).ok()) {
    return dir;
  }
  return string();
}

}  // namespace devtools_goma
