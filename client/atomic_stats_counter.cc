// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "atomic_stats_counter.h"

#include <stdint.h>

namespace devtools_goma {

StatsCounter::StatsCounter() : value_(0) {
}

void StatsCounter::Clear() {
#ifndef _WIN32
  __atomic_store_n(&value_, 0, __ATOMIC_RELAXED);
#else
  _InterlockedExchange64(&value_, 0);
#endif  // _WIN32
}

int64_t StatsCounter::value() const {
#ifndef _WIN32
  return __atomic_load_n(&value_, __ATOMIC_RELAXED);
#elif defined(_WIN64)
  // In x64, as far as the value is properly 64bit aligned,
  // 64bit read is atomic.
  // To guarantee the value is read from memory, volatile is used here.
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms684122(v=vs.85).aspx
  return *const_cast<const volatile int64_t*>(&value_);
#else
  #error "Windows 32bit environment is not supported"
#endif  // _WIN32
}

}  // namespace devtools_goma
