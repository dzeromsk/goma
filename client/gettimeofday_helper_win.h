// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GETTIMEOFDAY_HELPER_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_GETTIMEOFDAY_HELPER_WIN_H_

#include "config_win.h"
#include <winsock2.h>  // for timeval

namespace devtools_goma {

struct timezone {
  int tz_minuteswest;  // minutes west of Greenwich
  int tz_dsttime;      // type of DST correction
};

int gettimeofday(struct timeval* tv, struct timezone* tz);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GETTIMEOFDAY_HELPER_WIN_H_
