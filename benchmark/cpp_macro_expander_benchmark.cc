// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "benchmark/benchmark.h"
#include "cxx/include_processor/cpp_macro_expander.h"
#include "cxx/include_processor/cpp_parser.h"
#include "cxx/include_processor/cpp_token.h"
#include "cxx/include_processor/cpp_tokenizer.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"

namespace devtools_goma {

void BM_MacroExpandRecursive(benchmark::State& state) {
  std::ostringstream os;
  os << "#define F0(x) x" << std::endl;
  for (int i = 1; i < 100; ++i) {
    os << "#define F" << i << "(x) F" << (i - 1) << "(x) + 1" << std::endl;
  }

  CppParser cpp_parser;
  cpp_parser.AddStringInput(os.str(), "(string)");
  cpp_parser.ProcessDirectives();

  ArrayTokenList tokens;
  CHECK(CppTokenizer::TokenizeAll("F99(1)", SpaceHandling::kKeep, &tokens));

  // Test expectedly converted.
  {
    ArrayTokenList expected;
    for (int i = 0; i < 99; ++i) {
      expected.push_back(CppToken(1));
      expected.back().string_value = "1";
      expected.push_back(CppToken(CppToken::ADD));
      expected.back().string_value = "+";
    }
    expected.push_back(CppToken(1));

    ArrayTokenList actual =
        CppMacroExpander(&cpp_parser).Expand(tokens, SpaceHandling::kSkip);
    CHECK_EQ(expected, actual);
  }

  for (auto _ : state) {
    (void)_;

    (void)CppMacroExpander(&cpp_parser).Expand(tokens, SpaceHandling::kSkip);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_MacroExpandRecursive);

}  // namespace devtools_goma

BENCHMARK_MAIN();
