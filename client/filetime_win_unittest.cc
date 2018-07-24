// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "filetime_win.h"

#include <cstdint>

#include <gtest/gtest.h>

#include "absl/time/time.h"

namespace devtools_goma {

// Windows FILETIME is based on number of 100-ns intervals since 1601 Jan 01.

// Make sure that the two DWORDS of the filetime are both 32-bit ints.
static_assert(sizeof(FILETIME::dwLowDateTime) == sizeof(uint32_t), "");
static_assert(sizeof(FILETIME::dwHighDateTime) == sizeof(uint32_t), "");

TEST(FiletimeWinTest, ConvertFiletimeToUnixTime) {
  // Test against the start of 2000 Jan 01.

  // FILETIME timestamp representing 2000 Jan 01. Verified using the online
  // conversion tool: https://www.epochconverter.com/ldap
  constexpr uint64_t kFiletimeValue = 125911584000000000ULL;

  FILETIME filetime;
  filetime.dwLowDateTime = kFiletimeValue & UINT32_MAX;
  filetime.dwHighDateTime = kFiletimeValue >> 32;

  // Number of seconds from the Unix Epoch to 2000 Jan 01. Verified using the
  // online conversion tool: https://www.epochconverter.com/
  constexpr uint64_t kUnixTimeValue = 946684800;

  EXPECT_EQ(kUnixTimeValue,
            absl::ToUnixSeconds(ConvertFiletimeToAbslTime(filetime)));
}

TEST(FiletimeWinTest, ConvertFiletimeToAbslTime) {
  // Similar to the previous time, but with a nonzero nanosecond component.
  // The "1234" represents blocks of 100 ns, so it is 123400 ns.
  constexpr uint64_t kFiletimeValue = 125911584000001234ULL;

  FILETIME filetime;
  filetime.dwLowDateTime = kFiletimeValue & UINT32_MAX;
  filetime.dwHighDateTime = kFiletimeValue >> 32;

  absl::Time time_value = ConvertFiletimeToAbslTime(filetime);
  absl::Time::Breakdown breakdown = time_value.In(absl::UTCTimeZone());

  EXPECT_EQ(2000, breakdown.year);
  EXPECT_EQ(1, breakdown.month);
  EXPECT_EQ(1, breakdown.day);
  EXPECT_EQ(0, breakdown.hour);
  EXPECT_EQ(0, breakdown.minute);
  EXPECT_EQ(0, breakdown.second);
  EXPECT_EQ(absl::Nanoseconds(123400), breakdown.subsecond);
}

}  // namespace devtools_goma
