// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILE_STATS_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILE_STATS_H_

#include <stdint.h>
#include <string>

#include "compiler_specific.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_log.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

class CompileStats : public ExecLog {
 public:
  CompileStats();
  ~CompileStats();

  size_t gcc_req_size;
  size_t gcc_resp_size;

  size_t input_file_rpc_size;
  size_t input_file_rpc_raw_size;

  size_t output_file_rpc;
  int64_t output_file_rpc_req_build_time;
  int64_t output_file_rpc_req_send_time;
  int64_t output_file_rpc_wait_time;
  int64_t output_file_rpc_resp_recv_time;
  int64_t output_file_rpc_resp_parse_time;
  size_t output_file_rpc_size;
  size_t output_file_rpc_raw_size;

  string major_factor() const;
};

int64_t SumRepeatedInt32(
    const google::protobuf::RepeatedField<google::protobuf::int32>&
    repeated_int32);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILE_STATS_H_
