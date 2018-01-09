// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_ATOMIC_STATS_COUNTER_H_
#define DEVTOOLS_GOMA_CLIENT_ATOMIC_STATS_COUNTER_H_

#include <stdint.h>

#ifdef _WIN32
# include <intrin.h>
#endif

namespace devtools_goma {

class StatsCounter {
 public:
  StatsCounter();

  StatsCounter(const StatsCounter&) = delete;
  StatsCounter& operator=(const StatsCounter&) = delete;

  void Add(int64_t n);
  void Clear();
  int64_t value() const;
 private:
  int64_t value_;
};

inline void StatsCounter::Add(int64_t n) {
#ifndef _WIN32
  __atomic_add_fetch(&value_, n, __ATOMIC_RELAXED);
#else
  _InterlockedExchangeAdd64(&value_, n);
#endif  // _WIN32
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_ATOMIC_STATS_COUNTER_H_
