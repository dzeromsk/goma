// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "benchmark/benchmark.h"
#include "cxx/include_processor/cpp_parser.h"
#include "glog/logging.h"

using std::string;

namespace devtools_goma {

void BM_ReadObjectMacro(benchmark::State& state) {
  string long_expr;

  for (int i = 0; i < state.range(0); ++i) {
    long_expr += " long_long_expr_" + std::to_string(i);
  }

  string directives;
  for (int i = 0; i < state.range(0); ++i) {
    directives +=
        "#define long_long_macro_" + std::to_string(i) + long_expr + "\n";
  }

  for (auto _ : state) {
    (void)_;
    CppParser cpp_parser;
    cpp_parser.AddStringInput(directives, "a.cc");
    CHECK(cpp_parser.ProcessDirectives());
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ReadObjectMacro)->RangeMultiplier(2)->Range(1, 16);

void BM_ReadFunctionMacro(benchmark::State& state) {
  string long_expr;

  for (int i = 0; i < state.range(0); ++i) {
    long_expr += " long_long_expr_" + std::to_string(i);
  }

  string directives;
  for (int i = 0; i < state.range(0); ++i) {
    directives += "#define long_long_macro_" + std::to_string(i) + "()" +
                  long_expr + "\n";
  }

  for (auto _ : state) {
    (void)_;
    CppParser cpp_parser;
    cpp_parser.AddStringInput(directives, "a.cc");
    CHECK(cpp_parser.ProcessDirectives());
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ReadFunctionMacro)->RangeMultiplier(2)->Range(1, 32);

}  // namespace devtools_goma

BENCHMARK_MAIN();
