// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "time_util.h"

#include <algorithm>
#include <array>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"

namespace devtools_goma {

namespace {

// See explanation of this value in comments of ComputeDataRateInKBps().
const int64_t kMsToNsRatio = absl::Milliseconds(1) / absl::Nanoseconds(1);

// Rounds an absl::Duration to the nearest unit indicated by |unit|. Can round
// up or down, depending on which is closer.
absl::Duration RoundDuration(absl::Duration time, absl::Duration unit) {
  DCHECK_GT(unit, absl::ZeroDuration());
  return absl::Trunc(time + unit / 2, unit);
}

// Same as absl::FormatDuration, but adds a space before the "ms", "us", "ns",
// or "s".
// |unit| is the nearest unit to which to round |time|.
std::string FormatDurationWithSpace(absl::Duration time, absl::Duration unit) {
  std::string result = absl::FormatDuration(RoundDuration(time, unit));
  const size_t len = result.size();

  DCHECK_GT(len, 0);

  // Do not modify the string if it was not properly formed, too short, or "0".
  if (result[len - 1] != 's' || len < 2) {
    return result;
  }

  // Insert a space if it was "ms", "us", or "ns".
  if (result[len - 2] == 'm' || result[len - 2] == 'u' ||
      result[len - 2] == 'n') {
    return result.insert(len - 2, 1, ' ');
  }

  // Otherwise, make sure that the digit before the 's' was a digit.
  if (absl::ascii_isdigit(result[len - 2])) {
    return result.insert(len - 1, 1, ' ');
  }

  return result;
}

}  // namespace

int64_t ComputeDataRateInKBps(int64_t num_bytes, absl::Duration time) {
  if (time <= absl::ZeroDuration()) {
    return -1;
  }

  // Explanation of the computation, where N = |num_bytes| and T = |time|:
  //
  // Computation of bytes per second: N / ToSeconds(T)
  //
  //                             N           1 kB               N
  // Kilobytes per second: ------------ x ---------- = -------------------
  //                       ToSeconds(T)   1000 bytes   ToSeconds(T) * 1000
  //
  // ToSeconds(T) * 1000 = ToMillisec(T). Thus "N bytes per millisecond" is
  // equivalent to "N kilobytes per second."
  //
  // So the computation becomes: N / ToMillisec(T).
  //
  // But if |T| < 1 ms, this value will be rounded down to 0, resulting in a
  // division by 0. To avoid this, do the following:
  //
  //       N         1000000        N * 1000000
  // ------------- * ------- = -----------------------
  // ToMillisec(T)   1000000   ToMillisec(T) * 1000000
  //
  // Once again, note that ToMillisec(T) * 1000000 = ToNanosec(T).
  //
  // Now kilobytes per second is calculated as: N * 1000000 / ToNanosec(T).
  //
  // The "1000000" is just the ratio of a millisecond to a nanosecond.
  return num_bytes * kMsToNsRatio / ToInt64Nanoseconds(time);
}

int DurationToIntMs(absl::Duration duration) {
  return static_cast<int>(absl::ToInt64Milliseconds(
      RoundDuration(duration, absl::Milliseconds(1))));
}

std::string FormatDurationInMilliseconds(absl::Duration time) {
  return absl::StrCat(
      absl::ToInt64Milliseconds(RoundDuration(time, absl::Milliseconds(1))),
      " ms");
}

std::string FormatDurationInMicroseconds(absl::Duration time) {
  return absl::StrCat(
      absl::ToInt64Microseconds(RoundDuration(time, absl::Microseconds(1))),
      " us");
}

std::string FormatDurationToThreeDigits(absl::Duration time) {
  // This code assumes that absl::Duration does not have resolution less than
  // nanoseconds.

  // An array of durations that contains increasing time resolution units, in
  // powers of 10 starting from 1 ns. This must be in sorted order because it is
  // searched by std::upper_bound().
  constexpr std::array<absl::Duration, 9> kTimeResolutions = {
    absl::Nanoseconds(1),
    absl::Nanoseconds(10),
    absl::Nanoseconds(100),
    absl::Microseconds(1),
    absl::Microseconds(10),
    absl::Microseconds(100),
    absl::Milliseconds(1),
    absl::Milliseconds(10),
    absl::Milliseconds(100),
  };

  // Find the resolution required to print |time| with no more than three
  // significant figures.
  auto iterator = std::upper_bound(kTimeResolutions.begin(),
                                   kTimeResolutions.end(), time / 1000);
  const absl::Duration resolution =
      iterator != kTimeResolutions.end() ? *iterator : absl::Seconds(1);

  if (time < absl::Minutes(1)) {
    return FormatDurationWithSpace(time, resolution);
  }

  // If the formatted time includes minutes or anything larger, just use the
  // regular FormatDuration().
  return absl::FormatDuration(RoundDuration(time, resolution));
}

}  // namespace devtools_goma
