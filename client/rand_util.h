// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_RAND_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_RAND_UTIL_H_

#include <string>

#include "absl/time/time.h"

namespace devtools_goma {

std::string GetRandomAlphanumeric(size_t length);

// Returns a pseudorandomized duration value in the range [min, max].
absl::Duration RandomDuration(absl::Duration min, absl::Duration max);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_RAND_UTIL_H_
