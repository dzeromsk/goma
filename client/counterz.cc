// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "counterz.h"

#include <algorithm>

#include <glog/logging.h>

#include "absl/strings/match.h"
#include "file_helper.h"
#include "google/protobuf/util/json_util.h"

namespace devtools_goma {

struct CounterStat {
  std::string name;
  int64_t count;
  absl::Duration time;
};

void CounterInfo::Inc(absl::Duration time) {
  counter_.Add(1);
  // This must be kept as an integer number of nanoseconds because
  // total_time_in_ns_ is an atomic integer counter.
  total_time_in_ns_.Add(absl::ToInt64Nanoseconds(time));
}

void CounterInfo::Dump(CounterStat* stat) const {
  stat->name = location_ + "(" + funcname_ + ":" + name_ + ")";
  stat->count = counter_.value();
  stat->time = absl::Nanoseconds(total_time_in_ns_.value());
}

void CounterInfo::DumpToProto(CounterzStat* counterz) const {
  counterz->set_name(name_);
  counterz->set_function_name(funcname_);
  counterz->set_location(location_);

  counterz->set_total_count(counter_.value());
  counterz->set_total_time_ns(total_time_in_ns_.value());
}

void Counterz::DumpToJson(Json::Value* json) const {
  std::vector<CounterStat> stats(counters_.size());

  {
    AUTOLOCK(lock, &mu_);
    for (size_t i = 0; i < counters_.size(); ++i) {
      counters_[i]->Dump(&stats[i]);
    }
  }

  std::sort(stats.begin(), stats.end(),
            [](const CounterStat& l, const CounterStat& r) {
              return l.time > r.time;
            });

  *json = Json::Value(Json::arrayValue);

  for (const auto& stat : stats) {
    Json::Value value;
    value["name"] = stat.name;
    value["count"] = Json::Int64(stat.count);

    value["total time"] = absl::FormatDuration(stat.time);
    value["average time"] =
        absl::FormatDuration(stat.time / std::max<int64_t>(stat.count, 1));
    json->append(std::move(value));
  }
}

void Counterz::DumpToProto(CounterzStats* counters) const {
  AUTOLOCK(lock, &mu_);
  for (size_t i = 0; i < counters_.size(); ++i) {
    counters_[i]->DumpToProto(counters->add_counterz_stats());
  }
}

Counterz* Counterz::instance_;

/* static */
void Counterz::Init() {
  CHECK(instance_ == nullptr);
  instance_ = new Counterz;
}

/* static */
void Counterz::Dump(const std::string& filename) {
  CHECK(instance_ != nullptr);

  CounterzStats counterz;
  instance_->DumpToProto(&counterz);

  std::string dump_buf;
  if (absl::EndsWith(filename, ".json")) {
    google::protobuf::util::JsonPrintOptions options;
    options.preserve_proto_field_names = true;
    google::protobuf::util::MessageToJsonString(counterz, &dump_buf, options);
  } else {
    counterz.SerializeToString(&dump_buf);
  }
  if (!WriteStringToFile(dump_buf, filename)) {
    LOG(ERROR) << "failed to dump counterz stats to " << filename;
  } else {
    LOG(INFO) << "dumped counterz stats to " << filename;
  }
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
