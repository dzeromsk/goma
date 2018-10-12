// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_CXX_FLAGS_H_
#define DEVTOOLS_GOMA_LIB_CXX_FLAGS_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "lib/compiler_flags.h"
#include "lib/flag_parser.h"
using std::string;

namespace devtools_goma {

class CxxFlags : public CompilerFlags {
 public:
  // Returns true if the src language is C++ (not C).
  virtual bool is_cplusplus() const = 0;

 protected:
  CxxFlags(const std::vector<string>& args, const string& cwd)
      : CompilerFlags(args, cwd) {}

  template <bool is_defined>
  class MacroStore : public FlagParser::Callback {
   public:
    explicit MacroStore(std::vector<std::pair<string, bool>>* macros)
        : macros_(macros) {}

    // Returns parsed flag value of value for flag.
    string ParseFlagValue(const FlagParser::Flag& /* flag */,
                          const string& value) override {
      macros_->emplace_back(value, is_defined);
      return value;
    }

   private:
    std::vector<std::pair<string, bool>>* macros_;
  };
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_CXX_FLAGS_H_
