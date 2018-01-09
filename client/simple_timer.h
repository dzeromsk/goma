// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SIMPLE_TIMER_H_
#define DEVTOOLS_GOMA_CLIENT_SIMPLE_TIMER_H_

#ifdef _WIN32
# include <Windows.h>
#elif defined(__linux__)
# include <time.h>
#elif defined(__MACH__)
# include <cstdint>
#else
# error "unknown platform"
#endif

namespace devtools_goma {

class SimpleTimer {
 public:
  enum CtorFlag { NO_START, START };
  explicit SimpleTimer(CtorFlag cf);
  SimpleTimer();
  ~SimpleTimer();
  void Start();

  // Return elapsed time in seconds.
  double Get() const;

  // Returns elapsed time in milliseconds for short interval.
  // 2^31 / 1000 / 3600 / 24 ~= 24.8, so roughly it would round about 24 days.
  int GetInMs() const;

  // Return elapsed time in milliseconds.
  long long GetInMilliSeconds() const;

  // Return elapsed time in nanoseconds.
  long long GetInNanoSeconds() const;

 private:
#ifdef _WIN32
  LARGE_INTEGER start_time_;
  LARGE_INTEGER frequency_;
#elif defined(__linux__)
  struct timespec start_time_;
#elif defined(__MACH__)
  uint64_t start_time_;
#endif
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SIMPLE_TIMER_H_
