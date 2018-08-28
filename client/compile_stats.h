// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILE_STATS_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILE_STATS_H_

#include <stdint.h>
#include <string>

#include "absl/time/time.h"
#include "compiler_specific.h"
#include "http.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_log.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace Json {
class Value;
}  // namespace Json

namespace devtools_goma {

class ExecResp;

class CompileStats : public ExecLog {
 public:
  enum class DumpDetailLevel {
    kNotDetailed,
    kDetailed,
  };

  CompileStats();
  ~CompileStats();

  // Adds various stat values from |status| to the fields in this class.
  void AddStatsFromHttpStatus(const HttpClient::Status& status);
  // Adds various stat values from |resp| to the fields in this class.
  void AddStatsFromExecResp(const ExecResp& response);

  // Dumps various stat fields to JSON in human-readable format.  |detail_level|
  // determines the number of fields dumped -- more detailed means more fields
  // are dumped into |*json|.
  void DumpToJson(Json::Value* json, DumpDetailLevel detail_level) const;

  // Sets various fields in |*resp| based on CompileStats values.
  void StoreStatsInExecResp(ExecResp* resp) const;

  bool LocalCacheHit() const {
    return has_cache_source() && cache_source() == ExecLog::LOCAL_OUTPUT_CACHE;
  }

  size_t gomacc_req_size;
  size_t gomacc_resp_size;

  size_t input_file_rpc_size;
  size_t input_file_rpc_raw_size;

  size_t output_file_rpc;
  absl::Duration output_file_rpc_req_build_time;
  absl::Duration output_file_rpc_req_send_time;
  absl::Duration output_file_rpc_wait_time;
  absl::Duration output_file_rpc_resp_recv_time;
  absl::Duration output_file_rpc_resp_parse_time;
  size_t output_file_rpc_size;
  size_t output_file_rpc_raw_size;

  // in INIT.
  absl::Duration pending_time;

  // in SETUP.
  absl::Duration compiler_info_process_time;
  // include_preprocess_time is sum of
  // include_processor_wait_time and include_processor_run_time.
  absl::Duration include_preprocess_time;
  absl::Duration include_processor_wait_time;
  absl::Duration include_processor_run_time;

  // in FILE_REQ.
  absl::Duration include_fileload_time;
  absl::Duration include_fileload_pending_time;
  absl::Duration include_fileload_run_time;

  // in CALL_EXEC.  repeated by retry.
  absl::Duration total_rpc_call_time;
  absl::Duration total_rpc_throttle_time;
  absl::Duration total_rpc_pending_time;
  absl::Duration total_rpc_req_build_time;
  absl::Duration total_rpc_req_send_time;
  absl::Duration total_rpc_wait_time;
  absl::Duration total_rpc_resp_recv_time;
  absl::Duration total_rpc_resp_parse_time;


  // in FILE_RESP.
  absl::Duration file_response_time;
  absl::Duration output_file_time;

  // Total time elapsed for handling the request in compiler_proxy.
  absl::Duration handler_time;

  // Local run stats.
  absl::Duration local_pending_time;
  absl::Duration local_run_time;
  absl::Duration total_local_output_file_time;
  absl::Duration local_delay_time;

  // Returns the name of the compile task component that took the most time,
  // and how much time it took and the percentage of the overall time. If all
  // durations were zero, returns an empty string.
  string GetMajorFactorInfo() const;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILE_STATS_H_
