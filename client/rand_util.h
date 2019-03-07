// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_RAND_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_RAND_UTIL_H_

#include <string>

#include "absl/time/time.h"

namespace devtools_goma {

// My implementation of <random> cryptographically secure random number
// generator.
//
// Since Chromium C++11 does not allow us to use cryptographically secure
// random numbers in <random>, let me implement wrapper to RAND_bytes here.
// https://groups.google.com/a/chromium.org/d/msg/chromium-dev/t7vf5etS7cw/kZIeZUokAAAJ
class MyCryptographicSecureRNG {
 public:
  typedef unsigned int result_type;

  MyCryptographicSecureRNG() {}

  MyCryptographicSecureRNG(const MyCryptographicSecureRNG&) = delete;
  void operator=(const MyCryptographicSecureRNG&) = delete;

  ~MyCryptographicSecureRNG() {}

  static constexpr result_type min() {
    return std::numeric_limits<result_type>::min();
  }

  static constexpr result_type max() {
    return std::numeric_limits<result_type>::max();
  }

  result_type operator()();
};

std::string GetRandomAlphanumeric(size_t length);

// Returns a pseudorandomized duration value in the range [min, max].
absl::Duration RandomDuration(absl::Duration min, absl::Duration max);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_RAND_UTIL_H_
