// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SUBPROCESS_OPTION_SETTER_H_
#define DEVTOOLS_GOMA_CLIENT_SUBPROCESS_OPTION_SETTER_H_

#include "atomic_stats_counter.h"
#include "http.h"

namespace devtools_goma {

class SubProcessStats;

enum class BurstModeReason {
  NETWORK_ERROR,
  COMPILER_DISABLED,
};

class SubProcessOptionSetter {
 public:
  SubProcessOptionSetter() = delete;
  SubProcessOptionSetter(SubProcessOptionSetter&) = delete;
  SubProcessOptionSetter& operator=(const SubProcessOptionSetter&) = delete;

  SubProcessOptionSetter(int max_subprocs,
                         int max_subprocs_low_priority,
                         int max_subprocs_heavy_weight,
                         int burst_max_subprocs,
                         int burst_max_subprocs_low_priority,
                         int burst_max_subprocs_heavy_weight);
  ~SubProcessOptionSetter() {}

  void TurnOnBurstMode(BurstModeReason reason);
  void TurnOffBurstMode();

  void DumpStatsToProto(SubProcessStats* stats);

 private:
  const int max_subprocs_;
  const int max_subprocs_low_priority_;
  const int max_subprocs_heavy_weight_;
  const int burst_max_subprocs_;
  const int burst_max_subprocs_low_priority_;
  const int burst_max_subprocs_heavy_weight_;

  StatsCounter stats_count_burst_by_network_error_;
  StatsCounter stats_count_burst_by_compiler_disabled_;
};

class NetworkErrorMonitor : public HttpClient::NetworkErrorMonitor {
 public:
  NetworkErrorMonitor() = delete;
  NetworkErrorMonitor(NetworkErrorMonitor&) = delete;
  NetworkErrorMonitor& operator=(const NetworkErrorMonitor&) = delete;

  explicit NetworkErrorMonitor(SubProcessOptionSetter* option_setter) :
      option_setter_(option_setter) {}
  ~NetworkErrorMonitor() override {}

  void OnNetworkErrorDetected() override {
    option_setter_->TurnOnBurstMode(BurstModeReason::NETWORK_ERROR);
  }

  void OnNetworkRecovered() override {
    option_setter_->TurnOffBurstMode();
  }

 private:
  SubProcessOptionSetter* option_setter_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SUBPROCESS_OPTION_SETTER_H_
