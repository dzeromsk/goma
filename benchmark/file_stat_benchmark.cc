// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "benchmark/benchmark.h"
#include "file_stat.h"
#include "unittest_util.h"

namespace devtools_goma {

void BM_FileStatExist(benchmark::State& state) {
  TmpdirUtil tmpdir("file_stat");
  tmpdir.CreateEmptyFile("empty");
  const string& path = tmpdir.FullPath("empty");

  for (auto _ : state) {
    (void)_;
    FileStat file_stat(path);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_FileStatExist);

void BM_FileStatNotExist(benchmark::State& state) {
  TmpdirUtil tmpdir("file_stat");
  const string& path = tmpdir.FullPath("not_exist");

  for (auto _ : state) {
    (void)_;
    FileStat file_stat(path);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_FileStatNotExist);

}  // namespace devtools_goma

BENCHMARK_MAIN();
