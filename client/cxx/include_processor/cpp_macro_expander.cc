// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro_expander.h"

#include "cpp_macro_expander_cbv.h"
#include "cpp_macro_expander_naive.h"

namespace devtools_goma {

ArrayTokenList CppMacroExpander::Expand(const ArrayTokenList& input_tokens,
                                        bool skip_space) {
  ArrayTokenList result;

  // Try CBV one first.
  if (CppMacroExpanderCBV(parser_).ExpandMacro(input_tokens, skip_space,
                                               &result)) {
    return result;
  }

  // fallback to precise case.
  result.clear();
  CppMacroExpanderNaive(parser_).ExpandMacro(input_tokens, skip_space, &result);

  return result;
}

}  // namespace devtools_goma
