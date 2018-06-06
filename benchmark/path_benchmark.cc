// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>

#include "path.h"

void BM_JoinPathRespectAbsoluteJoin(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;
#ifdef _WIN32
    file::JoinPathRespectAbsolute(R"(C:\src\chromium\src\out\Release)",
                                  R"(..\..\base\hash.h)");
#else
    file::JoinPathRespectAbsolute("/home/user/src/chromium/out/Release",
                                  "../../base/hash.h");
#endif
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_JoinPathRespectAbsoluteJoin);

void BM_JoinPathRespectAbsoluteNotJoin(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;
#ifdef _WIN32
    file::JoinPathRespectAbsolute(R"(C:\src\chromium\src\out\Release)",
                                  R"(C:\src\chromium\src\third_party\)"
                                  R"(toolchain\sdk\sdklib.h)");
#else
    file::JoinPathRespectAbsolute("/home/user/src/chromium/out/Release",
                                  "/home/user/src/chromium/third_party/llvm/"
                                  "include/stddef.h");
#endif
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_JoinPathRespectAbsoluteNotJoin);

BENCHMARK_MAIN();
