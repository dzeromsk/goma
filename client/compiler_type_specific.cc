// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compiler_type_specific.h"

namespace devtools_goma {

// Ok means IncludeProcessor run correctly.
CompilerTypeSpecific::IncludeProcessorResult
CompilerTypeSpecific::IncludeProcessorResult::Ok(
    std::set<string> required_files) {
  IncludeProcessorResult result;
  result.ok = true;
  result.required_files = std::move(required_files);
  return result;
}

// ErrorToLog means IncludeProcessor didn't finish. However, it's an internal
// error, so compile task should be fallen back. Error is logged, but won't be
// shown to a user.
//
CompilerTypeSpecific::IncludeProcessorResult
CompilerTypeSpecific::IncludeProcessorResult::ErrorToLog(string error_reason) {
  IncludeProcessorResult result;
  result.ok = false;
  result.error_reason = std::move(error_reason);
  result.error_to_user = false;
  return result;
}

// ErrorToUser means IncludeProcessor didn't finish due to user's input.
// Error is delivered to a user.
CompilerTypeSpecific::IncludeProcessorResult
CompilerTypeSpecific::IncludeProcessorResult::ErrorToUser(string error_reason) {
  IncludeProcessorResult result;
  result.ok = false;
  result.error_reason = std::move(error_reason);
  result.error_to_user = true;
  return result;
}

}  // namespace devtools_goma
