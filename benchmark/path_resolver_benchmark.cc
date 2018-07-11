// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <benchmark/benchmark.h>

#include "path_resolver.h"

void BM_ResolvePath(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;
#ifdef _WIN32
    devtools_goma::PathResolver::ResolvePath(
        R"(c:\src\chromium\src\third_party\depot_tools\)"
        R"(win_toolchain\vs_files\1180cb75833ea365097e279efb2d5d7a42dee4b0\)"
        R"(win_sdk\bin\..\..\win_sdk\include\10.0.15063.0\um\windows.h)");
#else
    devtools_goma::PathResolver::ResolvePath(
        "gen/mojo/public/interfaces/bindings/"
        "native_struct.mojom-shared-internal.h");
    devtools_goma::PathResolver::ResolvePath(
        "../../mojo/public/cpp/bindings/string_data_view.h");
    devtools_goma::PathResolver::ResolvePath(
        "../../third_party/WebKit/Source/modules/webgl/"
        "WebGLVertexArrayObjectOES.cpp");
#endif
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_ResolvePath);

BENCHMARK_MAIN();
