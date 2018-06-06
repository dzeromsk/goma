// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_COMPILER_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_LIB_COMPILER_TYPE_SPECIFIC_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "compiler_type.h"
using std::string;

namespace devtools_goma {

class CompilerFlags;

// CompilerTypeSpecific is a collection of compiler type specific methods.
class CompilerTypeSpecific {
 public:
  explicit CompilerTypeSpecific(CompilerType type) : type_(type) {}

  // Creates CompilerTypeSpecific from argument
  // (usually argv[0] of command line).
  static CompilerTypeSpecific FromArg(absl::string_view arg);

  CompilerType type() const { return type_; }

  std::unique_ptr<CompilerFlags> NewCompilerFlags(
      const std::vector<string>& args,
      const string& cwd) const;
  string GetCompilerName(absl::string_view arg) const;

 private:
  const CompilerType type_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_TYPE_SPECIFIC_H_
