// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mypath_helper.h"

#include <windows.h>

#include "glog/logging.h"

namespace devtools_goma {

string GetPlatformSpecificTempDirectory() {
  char buf[MAX_PATH + 1];
  DWORD size = GetTempPathA(sizeof(buf), buf);
  if (size == 0) {
    LOG(WARNING) << "failed ot get temporary directory.";
    LOG_SYSRESULT(GetLastError());
    return string();
  }
  return buf;
}

}  // namespace devtools_goma
