// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_INCLUDE_GUARD_DETECTOR_H_
#define DEVTOOLS_GOMA_CLIENT_INCLUDE_GUARD_DETECTOR_H_

#include <string>

#include "cpp_directive.h"

using std::string;

namespace devtools_goma {

class IncludeGuardDetector {
 public:
  IncludeGuardDetector(const IncludeGuardDetector&) = delete;
  void operator=(const IncludeGuardDetector&) = delete;

  static string Detect(const CppDirectiveList& directives);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_INCLUDE_GUARD_DETECTOR_H_
