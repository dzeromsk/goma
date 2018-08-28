// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_INCLUDE_PROCESSOR_H_
#define DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_INCLUDE_PROCESSOR_H_

#include <set>
#include <string>

#include "fake_compiler_info.h"
#include "fake_flags.h"

using std::string;

namespace devtools_goma {

class FakeIncludeProcessor {
 public:
  bool Run(const string& trace_id,
           const FakeFlags& fake_flags,
           const FakeCompilerInfo& compiler_info,
           std::set<string>* required_files);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_INCLUDE_PROCESSOR_H_
