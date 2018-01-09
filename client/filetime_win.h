// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILETIME_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_FILETIME_WIN_H_

#ifndef _WIN32
#error Win32 only
#endif

#include <ctime>

#include "config_win.h"

namespace devtools_goma {

// FILETIME contains a 64-bit value representing the number of 100-nanosecond
// intervals since January 1, 1601 (UTC).
// time_t is the number of seconds since Januray 1, 1970 (UTC).
time_t ConvertFiletimeToUnixTime(const FILETIME& filetime);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FILETIME_WIN_H_
