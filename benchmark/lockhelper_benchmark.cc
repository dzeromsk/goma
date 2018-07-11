// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include "benchmark/benchmark.h"
#include "glog/logging.h"
#include "lockhelper.h"
#include "platform_thread.h"

namespace devtools_goma {

template <typename LockType, typename AutoLockType>
class IncrementThread : public PlatformThread::Delegate {
 public:
  IncrementThread(LockType* lock, int* x, int loop_num)
      : lock_(lock), x_(x), loop_num_(loop_num) {}

  void ThreadMain() override {
    for (int i = 0; i < loop_num_; ++i) {
      AutoLockType lock(lock_);
      ++*x_;
    }
  }

 private:
  LockType* lock_;
  int* x_;
  const int loop_num_;
};

using FastIncrement = IncrementThread<FastLock, AutoFastLock>;
using NormalIncrement = IncrementThread<Lock, AutoLock>;

}  // namespace devtools_goma

void BM_FastLock(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;

    const int thread_num = 8;
    const int loop_num = 1000;
    std::vector<devtools_goma::PlatformThreadHandle> thread_ids(thread_num);
    std::vector<std::unique_ptr<devtools_goma::FastIncrement>> incrementers;
    int x = 0;
    devtools_goma::FastLock lock;

    for (int i = 0; i < thread_num; ++i) {
      incrementers.emplace_back(
          new devtools_goma::FastIncrement(&lock, &x, loop_num));
    }

    for (int i = 0; i < thread_num; ++i) {
      devtools_goma::PlatformThread::Create(incrementers[i].get(),
                                            &thread_ids[i]);
    }

    for (int i = 0; i < thread_num; ++i) {
      devtools_goma::PlatformThread::Join(thread_ids[i]);
    }

    CHECK_EQ(loop_num * thread_num, x);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_FastLock);

void BM_NormalLock(benchmark::State& state) {
  for (auto _ : state) {
    (void)_;

    const int thread_num = 8;
    const int loop_num = 1000;
    std::vector<devtools_goma::PlatformThreadHandle> thread_ids(thread_num);
    std::vector<std::unique_ptr<devtools_goma::NormalIncrement>> incrementers;
    int x = 0;
    devtools_goma::Lock lock;

    for (int i = 0; i < thread_num; ++i) {
      incrementers.emplace_back(
          new devtools_goma::NormalIncrement(&lock, &x, loop_num));
    }

    for (int i = 0; i < thread_num; ++i) {
      devtools_goma::PlatformThread::Create(incrementers[i].get(),
                                            &thread_ids[i]);
    }

    for (int i = 0; i < thread_num; ++i) {
      devtools_goma::PlatformThread::Join(thread_ids[i]);
    }

    CHECK_EQ(loop_num * thread_num, x);
  }

  state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_NormalLock);

BENCHMARK_MAIN();
