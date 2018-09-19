// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_SPACE_HANDLING_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_SPACE_HANDLING_H_

namespace devtools_goma {

// SpaceHandling specifies whether space tokens should be skipped or not.
enum class SpaceHandling {
  kKeep,
  kSkip,
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_SPACE_HANDLING_H_
