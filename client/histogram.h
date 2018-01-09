// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_HISTOGRAM_H_
#define DEVTOOLS_GOMA_CLIENT_HISTOGRAM_H_

#include <assert.h>
#include <math.h>
#include <stdint.h>

#include <map>
#include <string>

using std::string;

namespace devtools_goma {

class DistributionProto;

class Histogram {
 public:
  // Construct an object which holds occurrence frequence information
  // in interval buckets of log(logbase). Default value for log base is 2.
  Histogram() : logbase_(2),
        min_max_is_set_(false),
        count_(0), sum_(0), sum_of_squares_(0) {}
  ~Histogram() {}

  void SetName(const string& name);

  // Resets statistics values.
  // It preserves logbase_.
  void Reset();

  void Add(int64_t value);

  // Log base can be modified before adding the first value.
  void SetLogBase(float logbase);

  int DetermineBucket(int64_t value) const;
  int64_t BucketValue(int n) const;
  int64_t min() const { return min_; }
  int64_t max() const { return max_; }
  int64_t sum() const { return sum_; }
  double sum_of_squares() const { return sum_of_squares_; }
  int64_t standard_deviation() const;
  int64_t mean() const { return sum_ / count_; }
  int64_t count() const { return count_; }
  const string& name() const { return name_; }
  string DebugString() const;
  void DumpToProto(DistributionProto* dist);

 private:
  string ManySharps(int64_t n) const;

  string name_;
  float logbase_;
  std::map<int, int64_t> buckets_;
  bool min_max_is_set_;
  int64_t min_;
  int64_t max_;
  int64_t count_;
  int64_t sum_;
  double sum_of_squares_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_HISTOGRAM_H_
