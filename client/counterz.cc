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

void CounterInfo::Dump(
    std::string* name, int64_t* time_ns, int64_t* count) const {
  *name = location_ + "(" + funcname_ + ":" + name_ + ")";
  *count = counter_.value();
  *time_ns = total_time_in_ns_.value();
}

void CounterInfo::DumpToProto(CounterzStat* counterz) const {
  counterz->set_name(name_);
  counterz->set_function_name(funcname_);
  counterz->set_location(location_);

  counterz->set_total_count(counter_.value());
  counterz->set_total_time_ns(total_time_in_ns_.value());
}

void Counterz::DumpToJson(Json::Value* json) const {
  struct stat {
    std::string name;
    int64_t count, time_ns;
  };

  std::vector<stat> stats;

  {
    AUTOLOCK(lock, &mu_);
    for (size_t i = 0; i < counters_.size(); ++i) {
      stat s;
      counters_[i]->Dump(&s.name, &s.time_ns, &s.count);
      stats.push_back(std::move(s));
    }
  }

  std::sort(stats.begin(), stats.end(),
            [](const stat& l, const stat& r) {
              return l.time_ns > r.time_ns;
            });

  *json = Json::Value(Json::arrayValue);

  for (const auto& s : stats) {
    Json::Value j;
    j["name"] = s.name;
    j["count"] = Json::Int64(s.count);

    // TODO: human readable representation.
    j["time(s)"] = Json::Value(s.time_ns / 1e9);
    j["avg(ms)"] = Json::Value(
        s.time_ns / 1e6 / std::max<int64_t>(s.count, 1));
    json->append(j);
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
