// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_SHA256HASH_HASHER_H_
#define DEVTOOLS_GOMA_CLIENT_SHA256HASH_HASHER_H_

#include "goma_hash.h"

namespace devtools_goma {

// SHA256HashValueHasher can be used for hash function of unordered_map.
struct SHA256HashValueHasher {
  size_t operator()(const SHA256HashValue& hash_value) const {
    return hash_value.Hash();
  }
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SHA256HASH_HASHER_H_
