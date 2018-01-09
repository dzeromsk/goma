// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "counterz.h"

#include <algorithm>

#include <glog/logging.h>

namespace devtools_goma {

void CounterInfo::Dump(
    std::string* name, int64_t* time_ns, int64_t* count) const {
  *name = name_;
  *count = counter_.value();
  *time_ns = total_time_in_ns_.value();
}

void Counterz::DumpToJson(Json::Value* json) const {
  *json = Json::Value(Json::objectValue);

  AUTOLOCK(lock, &mu_);
  for (size_t i = 0; i < counters_.size(); ++i) {
    std::string name;
    int64_t count, time_ns;
    counters_[i]->Dump(&name, &time_ns, &count);
    (*json)[name]["count"] = Json::Int64(count);

    // TODO: human readable representation.
    (*json)[name]["time(s)"] = Json::Value(time_ns / 1e9);
    (*json)[name]["avg(ms)"] = Json::Value(
        time_ns / 1e6 / std::max<int64_t>(count, 1));
  }
}

Counterz* Counterz::instance_;

/* static */
void Counterz::Init() {
  CHECK(instance_ == nullptr);
  instance_ = new Counterz;
}

/* static */
void Counterz::Quit() {
  CHECK(instance_ != nullptr);
  delete instance_;
  instance_ = nullptr;
}

/* static */
Counterz* Counterz::Instance() {
  return instance_;
}

}  // namespace devtools_goma
