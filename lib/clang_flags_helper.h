// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_CLANG_FLAGS_HELPER_H_
#define DEVTOOLS_GOMA_LIB_CLANG_FLAGS_HELPER_H_

#include <string>
#include <vector>


#include "absl/types/optional.h"
using std::string;

namespace devtools_goma {

// ClangFlagsHelper is helper class to read clang specific flags used in execreq
// normalizer.
class ClangFlagsHelper {
 public:
  explicit ClangFlagsHelper(const std::vector<string>& args);

  const absl::optional<string>& fdebug_compilation_dir() const {
    return fdebug_compilation_dir_;
  }

 private:
  absl::optional<string> fdebug_compilation_dir_;
};

}  // namespace devtools_goma
#endif  // DEVTOOLS_GOMA_LIB_CLANG_FLAGS_HELPER_H_
