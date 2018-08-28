// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "absl/time/clock.h"
#include "benchmark/benchmark.h"
#include "simple_timer.h"

namespace devtools_goma {

void BM_SimpleTimer(benchmark::State& state) {
  SimpleTimer timer;

  for (auto _ : state) {
    (void)_;
    timer.GetDuration();
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_SimpleTimer);

void BM_AbslGetCurrentTimeNanos(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;
    benchmark::DoNotOptimize(absl::GetCurrentTimeNanos());
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_AbslGetCurrentTimeNanos);

}  // namespace devtools_goma

BENCHMARK_MAIN();
