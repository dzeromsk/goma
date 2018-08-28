// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "time_util.h"

#include "gtest/gtest.h"

namespace devtools_goma {

TEST(TimeUtilTest, ComputeDataRateInKBps) {
  // Test short durations.
  EXPECT_EQ(1, ComputeDataRateInKBps(1, absl::Milliseconds(1)));
  EXPECT_EQ(20, ComputeDataRateInKBps(200, absl::Milliseconds(10)));
  EXPECT_EQ(3, ComputeDataRateInKBps(3000, absl::Seconds(1)));

  // Test very short durations.
  EXPECT_EQ(4000, ComputeDataRateInKBps(200, absl::Microseconds(50)));
  EXPECT_EQ(35000, ComputeDataRateInKBps(35, absl::Microseconds(1)));

  // Test very long durations.

  // 1000 kB in 10 seconds -> 100 kBps.
  EXPECT_EQ(100, ComputeDataRateInKBps(1000 * 1000, absl::Seconds(10)));
  // 24000 kB in 60 seconds -> 400 kBps.
  EXPECT_EQ(400, ComputeDataRateInKBps(24 * 1000 * 1000, absl::Minutes(1)));
  // 45000 kB in 900 seconds -> 50 kBps.
  EXPECT_EQ(50, ComputeDataRateInKBps(45 * 1000 * 1000, absl::Minutes(15)));
}

TEST(TimeUtilTest, DurationToIntMsExact) {
  // Try a wide range of duration values.
  for (int num_ms = 0; num_ms < 10000; num_ms += 3) {
    EXPECT_EQ(num_ms, DurationToIntMs(absl::Milliseconds(num_ms)));
  }
}

TEST(TimeUtilTest, DurationToIntMsRounding) {
  // Test with time values that have sub-millisecond components.
  EXPECT_EQ(99, DurationToIntMs(absl::Microseconds(99499)));
  EXPECT_EQ(100, DurationToIntMs(absl::Microseconds(99500)));
  EXPECT_EQ(100, DurationToIntMs(absl::Microseconds(99999)));
}

TEST(TimeUtilTest, DurationToIntMsIntegerSize) {
  static_assert(sizeof(int) != sizeof(int64_t), "");

  // This value will fit in a 32-bit int. It is intentionally declared as a
  // int64_t so that the static_assert() can catch the wrong value that will
  // result in an overflow.
  constexpr int64_t kIntNumberOfMilliseconds = 2 * 1000 * 1000 * 1000;
  static_assert(kIntNumberOfMilliseconds <= INT_MAX, "");
  // This value requires a 64-bit int for storage
  constexpr int64_t kInt64NumberOfMilliseconds = 8LL * 1000 * 1000 * 1000;
  static_assert(kInt64NumberOfMilliseconds > INT_MAX, "");
  static_assert(kInt64NumberOfMilliseconds <= INT64_MAX, "");

  EXPECT_EQ(kIntNumberOfMilliseconds,
            DurationToIntMs(absl::Milliseconds(kIntNumberOfMilliseconds)));
  // DurationToIntMs() will truncate the number of milliseconds.
  EXPECT_NE(kInt64NumberOfMilliseconds,
            DurationToIntMs(absl::Milliseconds(kInt64NumberOfMilliseconds)));
}

TEST(TimeUtilTest, FormatDurationDiffersFromAbsl) {
  // This test checks tat these functions behave differently from
  // absl::FormatDuration(). When this test starts failing, it means
  // absl::FormatDuration() has been modified to behave the same as the
  // functions here, and can replace these functions.
  EXPECT_NE(absl::FormatDuration(absl::Milliseconds(100)),
            FormatDurationInMilliseconds(absl::Milliseconds(100)));
  EXPECT_NE(absl::FormatDuration(absl::Microseconds(100)),
            FormatDurationInMilliseconds(absl::Microseconds(100)));
}

TEST(TimeUtilTest, FormatDurationInMilliseconds) {
  // 1234.567 ms.
  EXPECT_EQ("1235 ms",
            FormatDurationInMilliseconds(absl::Microseconds(1234567)));
  // 0.499999 ms.
  EXPECT_EQ("0 ms", FormatDurationInMilliseconds(absl::Nanoseconds(499999)));
  // 0.999999 ms.
  EXPECT_EQ("1 ms", FormatDurationInMilliseconds(absl::Nanoseconds(999999)));
  // 1.234 ms.
  EXPECT_EQ("1 ms", FormatDurationInMilliseconds(absl::Microseconds(1234)));

  // Must explicitly print units even for ZeroDuration().
  EXPECT_EQ("0 ms", FormatDurationInMilliseconds(absl::ZeroDuration()));
}

TEST(TimeUtilTest, FormatDurationInMicroseconds) {
  // 1234567 us.
  EXPECT_EQ("1234567 us",
            FormatDurationInMicroseconds(absl::Microseconds(1234567)));
  // 0.499 us.
  EXPECT_EQ("0 us", FormatDurationInMicroseconds(absl::Nanoseconds(499)));
  // 0.999 us.
  EXPECT_EQ("1 us", FormatDurationInMicroseconds(absl::Nanoseconds(999)));
  // 1.234 us.
  EXPECT_EQ("1 us", FormatDurationInMicroseconds(absl::Nanoseconds(1234)));

  // Must explicitly print units even for ZeroDuration().
  EXPECT_EQ("0 us", FormatDurationInMicroseconds(absl::ZeroDuration()));
}

TEST(TimeUtilTest, FormatDurationToThreeDigits) {
  // 1234.567 ms => 1.23 s
  EXPECT_EQ("1.23 s", FormatDurationToThreeDigits(absl::Microseconds(1234567)));
  // 1235 ms => 1.24 s
  EXPECT_EQ("1.24 s", FormatDurationToThreeDigits(absl::Microseconds(1235000)));

  // 123.4567 ms => 123 ms
  EXPECT_EQ("123 ms",
            FormatDurationToThreeDigits(absl::Nanoseconds(123456700)));
  // 123.5 ms => 124 ms
  EXPECT_EQ("124 ms",
            FormatDurationToThreeDigits(absl::Nanoseconds(123500000)));

  // 12.34567 ms => 12.3 ms
  EXPECT_EQ("12.3 ms",
            FormatDurationToThreeDigits(absl::Nanoseconds(12345670)));
  // 12.35 ms => 12.4 ms
  EXPECT_EQ("12.4 ms",
            FormatDurationToThreeDigits(absl::Nanoseconds(12350000)));

  // 1.234567 ms => 1.23 ms
  EXPECT_EQ("1.23 ms", FormatDurationToThreeDigits(absl::Nanoseconds(1234567)));
  // 1.235 ms => 1.24 ms
  EXPECT_EQ("1.24 ms", FormatDurationToThreeDigits(absl::Nanoseconds(1235000)));

  // 123.456 us => 123 us
  EXPECT_EQ("123 us", FormatDurationToThreeDigits(absl::Nanoseconds(123456)));
  // 123.5 us => 124 us
  EXPECT_EQ("124 us", FormatDurationToThreeDigits(absl::Nanoseconds(123500)));

  // 12.345 us => 12.3 us
  EXPECT_EQ("12.3 us", FormatDurationToThreeDigits(absl::Nanoseconds(12345)));
  // 12.35 us => 12.4 us
  EXPECT_EQ("12.4 us", FormatDurationToThreeDigits(absl::Nanoseconds(12350)));

  // 1.234 us => 1.23 us
  EXPECT_EQ("1.23 us", FormatDurationToThreeDigits(absl::Nanoseconds(1234)));
  // 1.235 us => 1.24 us
  EXPECT_EQ("1.24 us", FormatDurationToThreeDigits(absl::Nanoseconds(1235)));

  // 123 ns => 123 ns
  EXPECT_EQ("123 ns", FormatDurationToThreeDigits(absl::Nanoseconds(123)));

  // 999.999 ms => 1 s
  EXPECT_EQ("1 s", FormatDurationToThreeDigits(absl::Microseconds(999999)));
  // 999.999 us -> 1 ms
  EXPECT_EQ("1 ms", FormatDurationToThreeDigits(absl::Nanoseconds(999999)));

  // Large numbers of seconds.
  EXPECT_EQ("59.5 s", FormatDurationToThreeDigits(absl::Milliseconds(59499)));
  EXPECT_EQ("1m", FormatDurationToThreeDigits(absl::Milliseconds(59999)));
  EXPECT_EQ("8m20s", FormatDurationToThreeDigits(absl::Milliseconds(500050)));
  EXPECT_EQ("148h8m", FormatDurationToThreeDigits(absl::Minutes(8888)));

  // Not necessary to print units for ZeroDuration().
  EXPECT_EQ("0", FormatDurationToThreeDigits(absl::ZeroDuration()));
}

}  // namespace devtools_goma
