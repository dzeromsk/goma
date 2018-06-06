// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_TENSORFLOW_CORE_FLAT_SET_H_
#define THIRD_PARTY_TENSORFLOW_CORE_FLAT_SET_H_

// a wrapper header to include FlatSet to namespace devtools_goma.

#include "glog/logging.h"

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable: 4201)  // nonstandard extension used
#pragma warning(disable: 4334)  // result of 32 bit shift implicitly converted to 64 bit.
#endif

#include "tensorflow/core/lib/gtl/flat_set.h"

#ifdef _WIN32
#pragma warning(pop)
#endif

namespace devtools_goma {

using tensorflow::gtl::FlatSet;

}  // namespace devtools_goma

#endif  // THIRD_PARTY_TENSORFLOW_CORE_FLAT_MAP_H_
