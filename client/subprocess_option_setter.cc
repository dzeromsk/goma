// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <memory>

#include "subprocess_option_setter.h"

#include "compiler_specific.h"
#include "glog/logging.h"
#include "subprocess_controller_client.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_stats.pb.h"
#include "prototmp/subprocess.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

SubProcessOptionSetter::SubProcessOptionSetter(
    int max_subprocs,
    int max_subprocs_low_priority,
    int max_subprocs_heavy_weight,
    int burst_max_subprocs,
    int burst_max_subprocs_low_priority,
    int burst_max_subprocs_heavy_weight)
    : max_subprocs_(max_subprocs),
      max_subprocs_low_priority_(max_subprocs_low_priority),
      max_subprocs_heavy_weight_(max_subprocs_heavy_weight),
      burst_max_subprocs_(burst_max_subprocs),
      burst_max_subprocs_low_priority_(burst_max_subprocs_low_priority),
      burst_max_subprocs_heavy_weight_(burst_max_subprocs_heavy_weight) {
  LOG_IF(ERROR, max_subprocs < max_subprocs_low_priority)
      << "should be max_subproc >= max_subprocs_low_priority.";
  LOG_IF(ERROR, max_subprocs < max_subprocs_heavy_weight)
      << "should be max_subproc >= max_subprocs_heavy_weight.";
  LOG_IF(ERROR, burst_max_subprocs < burst_max_subprocs_low_priority)
      << "should be burst_max_subproc >= burst_max_subprocs_low_priority.";
  LOG_IF(ERROR, burst_max_subprocs < burst_max_subprocs_heavy_weight)
      << "should be burst_max_subproc >= burst_max_subprocs_heavy_weight.";
}

void SubProcessOptionSetter::TurnOnBurstMode(BurstModeReason reason) {
  if (!SubProcessControllerClient::IsRunning())
    return;

  switch (reason) {
  case BurstModeReason::NETWORK_ERROR:
    stats_count_burst_by_network_error_.Add(1);
    break;
  case BurstModeReason::COMPILER_DISABLED:
    stats_count_burst_by_compiler_disabled_.Add(1);
    break;
  default:
    LOG(ERROR) << "unknown burst mode reason: "
               << static_cast<int>(reason);
    break;
  }

  std::unique_ptr<SubProcessSetOption> option(new SubProcessSetOption);
  option->set_max_subprocs(burst_max_subprocs_);
  option->set_max_subprocs_low_priority(burst_max_subprocs_low_priority_);
  option->set_max_subprocs_heavy_weight(burst_max_subprocs_heavy_weight_);
  SubProcessControllerClient::Get()->SetOption(std::move(option));
}

void SubProcessOptionSetter::TurnOffBurstMode() {
  if (!SubProcessControllerClient::IsRunning())
    return;

  std::unique_ptr<SubProcessSetOption> option(new SubProcessSetOption);
  option->set_max_subprocs(max_subprocs_);
  option->set_max_subprocs_low_priority(max_subprocs_low_priority_);
  option->set_max_subprocs_heavy_weight(max_subprocs_heavy_weight_);
  SubProcessControllerClient::Get()->SetOption(std::move(option));
}

void SubProcessOptionSetter::DumpStatsToProto(SubProcessStats* stats) {
  stats->set_count_burst_by_network_error(
      stats_count_burst_by_network_error_.value());
  stats->set_count_burst_by_compiler_disabled(
      stats_count_burst_by_compiler_disabled_.value());
}

}  // namespace devtools_goma
