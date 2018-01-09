// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <time.h>

#include "gettimeofday_helper_win.h"

namespace devtools_goma {

// TODO: Modify following function to be test-able.
int gettimeofday(struct timeval* tv, struct timezone* tz) {
  // Define a structure to receive the current Windows filetime
  FILETIME ft;

  // Initialize the present time to 0 and the timezone to UTC
  unsigned __int64 tmpres = 0;
  static int tzflag = 0;

  if (nullptr != tv) {
    GetSystemTimeAsFileTime(&ft);

    // The GetSystemTimeAsFileTime returns the number of 100 nanosecond
    // intervals since Jan 1, 1601 in a structure. Copy the high bits to
    // the 64 bit tmpres, shift it left by 32 then or in the low 32 bits.
    tmpres |= ft.dwHighDateTime;
    tmpres <<= 32;
    tmpres |= ft.dwLowDateTime;

    // Convert to microseconds by dividing by 10
    tmpres /= 10;

    // The Unix epoch starts on Jan 1 1970.  Need to subtract the difference
    // in seconds from Jan 1 1601.
    tmpres -= DELTA_EPOCH_IN_MICROSECS;

    // Finally change microseconds to seconds and place in the seconds value.
    // The modulus picks up the microseconds.
    tv->tv_sec = (long)(tmpres / 1000000UL);
    tv->tv_usec = (long)(tmpres % 1000000UL);
  }

  if (nullptr != tz) {
    if (!tzflag) {
      _tzset();
      tzflag++;
    }

    // Adjust for the timezone west of Greenwich
    long timezone;
    if (_get_timezone(&timezone) == 0)
      tz->tz_minuteswest = timezone / 60;
    int daylight_hours;
    if (_get_daylight(&daylight_hours) == 0)
      tz->tz_dsttime = daylight_hours;
  }

  return 0;
}

}  // namespace devtools_goma
