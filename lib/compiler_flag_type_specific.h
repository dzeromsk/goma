// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_COMPILER_FLAG_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_LIB_COMPILER_FLAG_TYPE_SPECIFIC_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "lib/compiler_flag_type.h"
#include "lib/execreq_normalizer.h"
using std::string;

namespace devtools_goma {

class CompilerFlags;

// CompilerFlagTypeSpecific is a collection of compiler type specific methods.
class CompilerFlagTypeSpecific {
 public:
  explicit CompilerFlagTypeSpecific(CompilerFlagType type) : type_(type) {}

  // Creates CompilerFlagTypeSpecific from argument
  // (usually argv[0] of command line).
  static CompilerFlagTypeSpecific FromArg(absl::string_view arg);

  // Gets CompilerName from argument.
  static string GetCompilerNameFromArg(absl::string_view arg) {
    return FromArg(arg).GetCompilerName(arg);
  }

  CompilerFlagType type() const { return type_; }

  std::unique_ptr<CompilerFlags> NewCompilerFlags(
      const std::vector<string>& args,
      const string& cwd) const;
  string GetCompilerName(absl::string_view arg) const;

  // Creates ExecReqNormalizer by type.
  std::unique_ptr<ExecReqNormalizer> NewExecReqNormalizer() const;

 private:
  const CompilerFlagType type_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_FLAG_TYPE_SPECIFIC_H_
