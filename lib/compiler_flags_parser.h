// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_PARSER_H_
#define DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_PARSER_H_

#include <memory>
#include <string>
#include <vector>

#include "compiler_flags.h"
using std::string;

namespace devtools_goma {

class CompilerFlagsParser {
 public:
  // Returns new instance of subclass of CompilerFlags based on |args|.
  // Returns NULL if args is empty or args[0] is unsupported command.
  static std::unique_ptr<CompilerFlags> New(const std::vector<string>& args,
                                            const string& cwd);

  // MustNew is like New but causes FATAL crash if New returns NULL.
  static std::unique_ptr<CompilerFlags> MustNew(const std::vector<string>& args,
                                                const string& cwd);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_PARSER_H_
