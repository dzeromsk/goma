// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rand_util.h"

#include <openssl/err.h>
#include <openssl/rand.h>
#include <limits>
#include <random>

#include "glog/logging.h"

namespace {

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

  result_type operator()() {
    result_type buf;
    CHECK(RAND_bytes(reinterpret_cast<uint8_t*>(&buf), sizeof buf) == 1)
        << "BoringSSL's RAND_bytes must not fail to get random. "
        << ERR_get_error();
    return buf;
  }
};

}  // namespace

namespace devtools_goma {

std::string GetRandomAlphanumeric(size_t length) {
  static const char kAlphanumericTable[] =
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
  DCHECK_EQ(62, sizeof(kAlphanumericTable) - 1);

  MyCryptographicSecureRNG gen;
  std::uniform_int_distribution<> dis(0, sizeof(kAlphanumericTable) - 2);

  std::string buf;
  buf.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    buf += kAlphanumericTable[dis(gen)];
  }
  return buf;
}

absl::Duration RandomDuration(absl::Duration min, absl::Duration max) {
  DCHECK_LE(min, max);
  const int64_t min_ns = absl::ToInt64Nanoseconds(min);
  const int64_t max_ns = absl::ToInt64Nanoseconds(max);

  MyCryptographicSecureRNG generator;
  std::uniform_int_distribution<> distribution(min_ns, max_ns);
  return absl::Nanoseconds(distribution(generator));
}

}  // namespace devtools_goma
