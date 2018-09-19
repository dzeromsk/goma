// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_H_

#include "cpp_parser.h"
#include "cpp_token.h"
#include "space_handling.h"

namespace devtools_goma {

class CppMacroExpander {
 public:
  explicit CppMacroExpander(CppParser* parser) : parser_(parser) {}

  ArrayTokenList Expand(const ArrayTokenList& input_tokens,
                        SpaceHandling space_handling);

 private:
  CppParser* parser_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_H_
