// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <random>
#include <unordered_map>

#include "absl/container/flat_hash_map.h"
#include "benchmark/benchmark.h"

namespace devtools_goma {

template <class M>
void BM_HashMap(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;

    M m;
    for (int i = 0; i < state.range(0); ++i) {
      m[i] = i;
    }
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(BM_HashMap, std::unordered_map<int, int>)
    ->RangeMultiplier(4)
    ->Range(1, 65536);
BENCHMARK_TEMPLATE(BM_HashMap, absl::flat_hash_map<int, int>)
    ->RangeMultiplier(4)
    ->Range(1, 65536);

template <class M>
void BM_HashMapReserve(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;

    M m;
    m.reserve(state.range(0));
    for (int i = 0; i < state.range(0); ++i) {
      m[i] = i;
    }
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(BM_HashMapReserve, std::unordered_map<int, int>)
    ->RangeMultiplier(4)
    ->Range(1, 65536);
BENCHMARK_TEMPLATE(BM_HashMapReserve, absl::flat_hash_map<int, int>)
    ->RangeMultiplier(4)
    ->Range(1, 65536);

template <class M>
void BM_HashMapRandom(benchmark::State& state) {
  std::mt19937 mt;
  for (auto _ : state) {
    (void)_;

    M m;
    for (int i = 0; i < state.range(0); ++i) {
      m[mt()] = i;
    }
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK_TEMPLATE(BM_HashMapRandom, std::unordered_map<int, int>)
    ->RangeMultiplier(4)
    ->Range(1, 65536);
BENCHMARK_TEMPLATE(BM_HashMapRandom, absl::flat_hash_map<int, int>)
    ->RangeMultiplier(4)
    ->Range(1, 65536);

}  // namespace devtools_goma

BENCHMARK_MAIN();
