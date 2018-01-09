/*BINFMTCXX: -DTEST -L ./glog-0.3.1/.libs/ -lglog
 */
// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <algorithm>
#include <iomanip>
#include <string>
#include <sstream>
#include <vector>

#include "compiler_specific.h"
#include "glog/logging.h"
#include "histogram.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_stats.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

const int64_t kGraphWidth = 50;

void Histogram::SetName(const string& name) {
  name_ = name;
}

void Histogram::SetLogBase(float logbase) {
  CHECK_EQ(count_, 0) << name_ << ": SetLogBase must be called before Add";
  logbase_ = logbase;
}

void Histogram::Reset() {
  buckets_.clear();
  min_max_is_set_ = false;
  count_ = 0;
  sum_ = 0;
  sum_of_squares_ = 0;
}

void Histogram::Add(int64_t value) {
  buckets_[DetermineBucket(value)]++;
  if (!min_max_is_set_) {
    min_max_is_set_ = true;
    min_ = max_ = value;
  } else {
    if (value < min_) min_ = value;
    if (value > max_) max_ = value;
  }
  count_++;
  sum_ += value;
  sum_of_squares_ += ((double)value * value);
}

string Histogram::ManySharps(int64_t n) const {
  CHECK_GE(n, 0) << name_;
  CHECK_LE(n, kGraphWidth) << name_;

  string s(n, '#');
  return s;
}

string Histogram::DebugString() const {
  CHECK_GT(count_, 0)
      << name_
      << ": Histogram cannot be output unless there is at least one value";

  std::stringstream ss;
  ss << name_ << ": "
     << " Basic stats: count: " << count_
     << " sum: " << sum_
     << " min: " << min_
     << " max: " << max_
     << " mean: " << mean()
     << " stddev: " << standard_deviation()
     << "\n";
  int64_t largest = buckets_.begin()->second;
  for (const auto& it : buckets_) {
    if (largest < it.second) largest = it.second;
  }

  std::vector<std::pair<std::pair<std::string, std::string>,
                        std::string>> label_values;

  for (int i = DetermineBucket(min_); i <= DetermineBucket(max_); ++i) {
    std::stringstream min_key_ss;
    std::stringstream max_key_ss;
    std::stringstream value_ss;

    min_key_ss << BucketValue(i);
    max_key_ss << BucketValue(i + 1);

    string min_key = min_key_ss.str();
    string max_key = max_key_ss.str();

    if (buckets_.find(i) != buckets_.end()) {
      int64_t value = buckets_.find(i)->second;
      value_ss << ManySharps(
          static_cast<int64_t>(
              static_cast<double>(kGraphWidth)
              * static_cast<double>(value)
              / static_cast<double>(largest)))
         << value;
    }

    label_values.push_back(std::make_pair(std::make_pair(min_key, max_key),
                                          value_ss.str()));
  }

  size_t longest_min_label = 0;
  size_t longest_max_label = 0;
  if (!label_values.empty()) {
    longest_min_label = label_values.back().first.first.size();
    longest_max_label = label_values.back().first.second.size();
  }

  for (const auto& entry : label_values) {
    ss << "["
       << std::setw(longest_min_label) << std::right << entry.first.first
       << "-"
       << std::setw(longest_max_label) << std::right << entry.first.second
       << "]: "
       << std::left << entry.second << '\n';
  }

  return ss.str();
}

int Histogram::DetermineBucket(int64_t value) const {
  if (value < 0) {
    LOG(WARNING) << "value is negative:" << value << " for " << name_;
    value = 0;
  }

  if (value < 1)
    return 0;

  int bucket = static_cast<int>(log(static_cast<double>(value)) /
                                log(static_cast<double>(logbase_))) + 1;
  if (bucket < 0) {
    bucket = 0;
  }
  return bucket;
}

int64_t Histogram::BucketValue(int n) const {
  if (n < 0) {
    LOG(WARNING) << "value is negative:" << n << " for " << name_;
    n = 0;
  }

  if (n == 0)
    return 0;

  return static_cast<int64_t>(pow(logbase_, n - 1));
}

int64_t Histogram::standard_deviation() const {
  double squared_mean = (double)sum_ * sum_ / count_ / count_;
  return static_cast<int64_t>(sqrt(sum_of_squares_ / count_ - squared_mean));
}

void Histogram::DumpToProto(DistributionProto* dist) {
  dist->set_count(count_);
  dist->set_sum(sum_);
  dist->set_sum_of_squares(sum_of_squares_);
  dist->set_min(min_);
  dist->set_max(max_);

  dist->set_logbase(logbase_);
  for (int i = 0; i <= DetermineBucket(max_); ++i) {
    if (i < DetermineBucket(min_)) {
      dist->add_bucket_value(0);
      continue;
    }
    const auto& pos = buckets_.find(i);
    if (pos == buckets_.end()) {
      dist->add_bucket_value(0);
      continue;
    }
    dist->add_bucket_value(buckets_.find(i)->second);
  }
}

}  // namespace devtools_goma
