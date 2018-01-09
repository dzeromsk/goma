// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COUNTERZ_H_
#define DEVTOOLS_GOMA_CLIENT_COUNTERZ_H_

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "atomic_stats_counter.h"
#include "autolock_timer.h"
#include "json/json.h"
#include "lockhelper.h"

namespace devtools_goma {

class CounterInfo {
 public:
  CounterInfo(const char* const location,
              const char* const funcname,
              const char* const name) {
    name_.reserve(strlen(location) + strlen(funcname) + strlen(name) + 3);
    name_ += location;
    name_ += "(";
    name_ += funcname;
    name_ += ":";
    name_ += name;
    name_ += ")";
  }

  void Inc(int64_t time_ns) {
    counter_.Add(1);
    total_time_in_ns_.Add(time_ns);
  }

  void Dump(std::string* name, int64_t* time_ns, int64_t* count) const;

 private:
  CounterInfo(const CounterInfo&) = delete;
  CounterInfo& operator=(const CounterInfo&) = delete;

  std::string name_;
  StatsCounter counter_;
  StatsCounter total_time_in_ns_;
};

class Counterz {
 public:
  void DumpToJson(Json::Value* json) const;

  CounterInfo* NewCounterInfo(const char* const location,
                              const char* const funcname,
                              const char* const name) {
    AUTOLOCK(lock, &mu_);
    counters_.emplace_back(new CounterInfo(location, funcname, name));
    return counters_.back().get();
  }

  static void Init();
  static void Quit();
  static Counterz* Instance();

 private:
  Counterz() {}
  Counterz(const Counterz&) = delete;
  Counterz& operator=(const Counterz&) = delete;

  mutable Lock mu_;
  std::vector<std::unique_ptr<CounterInfo>> counters_;

  static Counterz* instance_;
};

// Takes walltime of scope and stores it to CounterInfo.
class ScopedCounter {
 public:
  explicit ScopedCounter(CounterInfo* counter_info)
      : counter_info_(counter_info), timer_(SimpleTimer::START) {
  }

  ~ScopedCounter() {
    counter_info_->Inc(timer_.GetInNanoSeconds());
  }

 private:
  ScopedCounter(const ScopedCounter&) = delete;
  ScopedCounter& operator=(const ScopedCounter&) = delete;

  CounterInfo* counter_info_;
  SimpleTimer timer_;
};

// If HAVE_COUNTERZ is defined, counterz is enabled.
#ifdef HAVE_COUNTERZ

#define GOMA_COUNTERZ_STRINGFY(i) #i
#define GOMA_COUNTERZ_STR(i) GOMA_COUNTERZ_STRINGFY(i)

#define GOMA_COUNTERZ_CAT(a, b) a ## b
#define GOMA_COUNTERZ_CONCAT(a, b) GOMA_COUNTERZ_CAT(a, b)
#define GOMA_COUNTERZ_INFO_VAR_NAME(name) GOMA_COUNTERZ_CONCAT(name, __LINE__)

#define GOMA_COUNTERZ(name)                                                \
  static CounterInfo* GOMA_COUNTERZ_INFO_VAR_NAME(counter_info) =          \
      Counterz::Instance() == nullptr ?                                    \
          nullptr : Counterz::Instance()->NewCounterInfo(                  \
              __FILE__ ":" GOMA_COUNTERZ_STR(__LINE__), __func__, (name)); \
  ScopedCounter GOMA_COUNTERZ_INFO_VAR_NAME(scoped_counter)                \
      (GOMA_COUNTERZ_INFO_VAR_NAME(counter_info));

#else

#define GOMA_COUNTERZ_N(name, n)
#define GOMA_COUNTERZ(name)

#endif  // HAVE_COUNTERZ

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COUNTERZ_H_
