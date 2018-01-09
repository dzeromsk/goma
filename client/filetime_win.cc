// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "filetime_win.h"

namespace devtools_goma {

time_t ConvertFiletimeToUnixTime(const FILETIME& filetime) {
  ULARGE_INTEGER ull;
    ull.LowPart = filetime.dwLowDateTime;
    ull.HighPart = filetime.dwHighDateTime;
    return (ull.QuadPart / PRECISION_DIVIDER)
        - (DELTA_EPOCH_IN_MICROSECS / 1000000);  // time_t is in seconds.
}

}  // namespace devtools_goma
