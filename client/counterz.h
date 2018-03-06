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
#include "compiler_specific.h"
#include "json/json.h"
#include "lockhelper.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/counterz.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

class CounterInfo {
 public:
  CounterInfo(const char* const location,
              const char* const funcname,
              const char* const name)
      : location_(location), funcname_(funcname), name_(name) {
  }

  void Inc(int64_t time_ns) {
    counter_.Add(1);
    total_time_in_ns_.Add(time_ns);
  }

  void Dump(std::string* name, int64_t* time_ns, int64_t* count) const;
  void DumpToProto(CounterzStat* counterz) const;

 private:
  CounterInfo(const CounterInfo&) = delete;
  CounterInfo& operator=(const CounterInfo&) = delete;

  const std::string location_;
  const std::string funcname_;
  const std::string name_;
  StatsCounter counter_;
  StatsCounter total_time_in_ns_;
};

class Counterz {
 public:
  void DumpToJson(Json::Value* json) const;
  void DumpToProto(CounterzStats* counters) const;

  CounterInfo* NewCounterInfo(const char* const location,
                              const char* const funcname,
                              const char* const name) {
    AUTOLOCK(lock, &mu_);
    counters_.emplace_back(new CounterInfo(location, funcname, name));
    return counters_.back().get();
  }

  static void Init();

  // Dump exports counterz stats to |filename| if |filename| is not empty.
  // If |filename| ends with ".json", it exports stat in json format,
  // otherwise stats is exported in binary protobuf.
  static void Dump(const std::string& filename);

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
    if (counter_info_ != nullptr) {
      counter_info_->Inc(timer_.GetInNanoSeconds());
    }
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
