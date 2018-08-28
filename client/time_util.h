// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_TIME_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_TIME_UTIL_H_

#include <string>

#include "absl/time/time.h"

namespace devtools_goma {

// Returns the data transmission rate in kB per second, given a data size and
// the time duration it took to transmit that data. Behavior is undefined if
// time <= absl::ZeroDuration().
int64_t ComputeDataRateInKBps(int64_t num_bytes, absl::Duration time);

// Convenience function to convert absl::Duration to number of milliseconds as
// an int (not as an int64_t). Also rounds to the nearest whole number of ms.
//
// This is useful for assigning to int32 protobuf fields and avoiding a compile
// warning when truncating the int64 value. This can result in a truncation of
// the number of milliseconds if |duration| is greater than 24.855 days
// (INT_MAX milliseconds).
int DurationToIntMs(absl::Duration duration);

// Returns a formatted string representation of |time| in milliseconds, rounded
// to an integer number of milliseconds.
std::string FormatDurationInMilliseconds(absl::Duration time);

// Returns a formatted string representation of |time| in microseconds, rounded
// to an integer number of microseconds.
std::string FormatDurationInMicroseconds(absl::Duration time);

// Returns a formatted string representation of |time| with units, rounded to
// the nearest three-significant-figure digit if there is a decimal component.
// For example:
// 1234.567 ms => 1.23 s
// 123.4567 ms => 123 ms
// 12.34567 ms => 12.3 ms
// 1.234567 ms => 1.23 ms
// 0.1234567 ms => 123 us
//
// For durations >= 1 minute, this is same as absl::FormatDuration().
std::string FormatDurationToThreeDigits(absl::Duration time);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TIME_UTIL_H_
