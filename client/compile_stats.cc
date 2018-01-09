// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compile_stats.h"

#include <sstream>

namespace devtools_goma {

CompileStats::CompileStats()
    : ExecLog(),
      gcc_req_size(0),
      gcc_resp_size(0),
      input_file_rpc_size(0),
      input_file_rpc_raw_size(0),
      output_file_rpc(0),
      output_file_rpc_req_build_time(0),
      output_file_rpc_req_send_time(0),
      output_file_rpc_wait_time(0),
      output_file_rpc_resp_recv_time(0),
      output_file_rpc_resp_parse_time(0),
      output_file_rpc_size(0),
      output_file_rpc_raw_size(0) {
}

CompileStats::~CompileStats() {
}

string CompileStats::major_factor() const {
  int64_t t = 0;
  const char* s = "";
  if (compiler_info_process_time() > t) {
    t = compiler_info_process_time();
    s = "compiler_info";
  }
  if (include_processor_wait_time() > t) {
    t = include_processor_wait_time();
    s = "include_processor_wait_time";
  }
  if (include_processor_run_time() > t) {
    t = include_processor_run_time();
    s = "include_processor_run_time";
  }
  if (include_fileload_time() > t) {
    t = include_fileload_time();
    s = "file_upload";
  }
  {
    int64_t sum_rpc_req_send_time = SumRepeatedInt32(rpc_req_send_time());
    int64_t sum_rpc_resp_recv_time = SumRepeatedInt32(rpc_resp_recv_time());
    if (sum_rpc_req_send_time > t) {
      t = sum_rpc_req_send_time;
      s = "rpc_req";
    }
    if (sum_rpc_resp_recv_time > t) {
      t = sum_rpc_resp_recv_time;
      s = "rpc_resp";
    }
  }
  if (file_response_time() > t) {
    t = file_response_time();
    s = "file_download";
  }
  std::ostringstream r;
  r << s;
  if (t > 0) {
    r << ":" << t << "ms";
    if (handler_time() > 0) {
      r << " [" << (t * 100 / handler_time()) << "%]";
    }
  }
  return r.str();
}

int64_t SumRepeatedInt32(
    const google::protobuf::RepeatedField<google::protobuf::int32>&
    repeated_int32) {
  int64_t sum = 0;
  for (const auto& iter : repeated_int32) {
    sum += iter;
  }
  return sum;
}

}  // namespace devtools_goma
