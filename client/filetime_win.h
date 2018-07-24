// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILETIME_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_FILETIME_WIN_H_

#ifndef _WIN32
#error Win32 only
#endif

#include "absl/time/time.h"
#include "config_win.h"

namespace devtools_goma {

// FILETIME contains a 64-bit value representing the number of 100-nanosecond
// intervals since January 1, 1601 (UTC).
absl::Time ConvertFiletimeToAbslTime(const FILETIME& filetime);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FILETIME_WIN_H_
