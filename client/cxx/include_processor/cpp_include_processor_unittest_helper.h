// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_INCLUDE_PROCESSOR_UNITTEST_HELPER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_INCLUDE_PROCESSOR_UNITTEST_HELPER_H_

#include <set>
#include <string>

using std::string;

namespace devtools_goma {

void CompareFiles(const std::set<string>& expected_files,
                  const std::set<string>& actual_files,
                  const std::set<string>& allowed_extra_files);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_INCLUDE_PROCESSOR_UNITTEST_HELPER_H_
