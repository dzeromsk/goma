// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compile_stats.h"

#include <utility>

#include <json/json.h>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "time_util.h"
#include "util.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

namespace {

using FormatDurationFunc = std::string(*)(absl::Duration);

// If |duration| is a nonzero absl::Duration, store it in |*json| with
// key=|key|.
void StoreDurationToJsonIfNotZero(const std::string& key,
                                  absl::Duration duration,
                                  Json::Value* json,
                                  FormatDurationFunc format_func =
                                      &FormatDurationToThreeDigits) {
  if (duration != absl::ZeroDuration()) {
    (*json)[key] = format_func(duration);
  }
}

// If |value| is a non-empty string, store it in |*json| with key=|key|.
void StoreStringToJsonIfNotEmpty(
    const std::string& key, const std::string& value, Json::Value* json) {
  if (!value.empty()) { (*json)[key] = value; }
}

// If |value| is a nonzero value, store it in |*json| with key=|key|.
void StoreIntToJsonIfNotZero(
    const std::string& key, int value, Json::Value* json) {
  if (value) { (*json)[key] = value; }
}
void StoreInt64ToJsonIfNotZero(
    const std::string& key, int64_t value, Json::Value* json) {
  if (value) { (*json)[key] = Json::Value::Int64(value); }
}

// If |value| is true, store it in |*json| with key=|key|.
void StoreBooleanToJsonIfTrue(
    const std::string key, bool value, Json::Value* json) {
  if (value) { (*json)[key] = "true"; }
}

// If the iterator range is of nonzero length, stores all values indicated by
// the range of iterator as a JSON array member of |*json| with key=|key|.
template <typename Iter>
void StoreArrayToJsonIfNotEmpty(const std::string& key, Iter begin, Iter end,
                                Json::Value* json) {
  if (begin == end) { return; }

  Json::Value array(Json::arrayValue);
  for (Iter iter = begin; iter != end; ++iter) { array.append(*iter); }
  (*json)[key] = std::move(array);
}

}  // namespace

CompileStats::CompileStats()
    : ExecLog(),
      gomacc_req_size(0),
      gomacc_resp_size(0),
      input_file_rpc_size(0),
      input_file_rpc_raw_size(0),
      output_file_rpc(0),
      output_file_rpc_size(0),
      output_file_rpc_raw_size(0) {}

CompileStats::~CompileStats() {
}

string CompileStats::GetMajorFactorInfo() const {
  absl::Duration major_factor_time;
  const char* major_factor_name = "";
  if (this->compiler_info_process_time > major_factor_time) {
    major_factor_time = this->compiler_info_process_time;
    major_factor_name = "compiler_info";
  }
  if (this->include_processor_wait_time > major_factor_time) {
    major_factor_time = this->include_processor_wait_time;
    major_factor_name = "include_processor_wait_time";
  }
  if (this->include_processor_run_time > major_factor_time) {
    major_factor_time = this->include_processor_run_time;
    major_factor_name = "include_processor_run_time";
  }
  if (this->include_fileload_time > major_factor_time) {
    major_factor_time = this->include_fileload_time;
    major_factor_name = "file_upload";
  }
  {
  }
  if (this->total_rpc_req_send_time > major_factor_time) {
    major_factor_time = this->total_rpc_req_send_time;
    major_factor_name = "rpc_req";
  }
  if (this->total_rpc_resp_recv_time > major_factor_time) {
    major_factor_time = this->total_rpc_resp_recv_time;
    major_factor_name = "rpc_resp";
  }
  if (this->file_response_time > major_factor_time) {
    major_factor_time = this->file_response_time;
    major_factor_name = "file_download";
  }

  if (major_factor_time <= absl::ZeroDuration()) {
    // This returns an empty string so that the caller can choose to ignore the
    // result. That is easier to do than if the return value were some non-empty
    // string such as "N/A".
    return "";
  }

  std::string time_percentage_string;
  if (this->handler_time > absl::ZeroDuration()) {
    time_percentage_string =
        absl::StrCat(" [", major_factor_time * 100 / this->handler_time, "%]");
  }
  return absl::StrCat(major_factor_name, ": ",
                      FormatDurationToThreeDigits(major_factor_time),
                      time_percentage_string);
}

void CompileStats::AddStatsFromHttpStatus(const HttpClient::Status& status) {
  add_rpc_master_trace_id(status.master_trace_id);

  if (status.master_trace_id.empty() ||
      status.master_trace_id == status.trace_id) {
    add_rpc_req_size(status.req_size);
    add_rpc_resp_size(status.resp_size);
    add_rpc_raw_req_size(status.raw_req_size);
    add_rpc_raw_resp_size(status.raw_resp_size);

    add_rpc_throttle_time(DurationToIntMs(status.throttle_time));
    add_rpc_pending_time(DurationToIntMs(status.pending_time));
    add_rpc_req_build_time(DurationToIntMs(status.req_build_time));
    add_rpc_req_send_time(DurationToIntMs(status.req_send_time));
    add_rpc_wait_time(DurationToIntMs(status.wait_time));
    add_rpc_resp_recv_time(DurationToIntMs(status.resp_recv_time));
    add_rpc_resp_parse_time(DurationToIntMs(status.resp_parse_time));

    this->total_rpc_throttle_time += status.throttle_time;
    this->total_rpc_pending_time += status.pending_time;
    this->total_rpc_req_build_time += status.req_build_time;
    this->total_rpc_req_send_time += status.req_send_time;
    this->total_rpc_wait_time += status.wait_time;
    this->total_rpc_resp_recv_time += status.resp_recv_time;
    this->total_rpc_resp_parse_time += status.resp_parse_time;
  }
}

void CompileStats::AddStatsFromExecResp(const ExecResp& response) {
}

void CompileStats::DumpToJson(Json::Value* json,
                              DumpDetailLevel detail_level) const {
  // This field needs to be strictly in milliseconds so that it can be sorted.
  StoreDurationToJsonIfNotZero("time", this->handler_time, json,
                               &FormatDurationInMilliseconds);

  if (LocalCacheHit()) {
    (*json)["cache"] = "local hit";
  } else if (cache_hit()) {
    (*json)["cache"] = "hit";
  }

  StoreStringToJsonIfNotEmpty("major_factor", GetMajorFactorInfo(), json);

  StoreStringToJsonIfNotEmpty("command_version_mismatch",
                              exec_command_version_mismatch(), json);
  StoreStringToJsonIfNotEmpty("command_binary_hash_mismatch",
                              exec_command_binary_hash_mismatch(), json);
  StoreStringToJsonIfNotEmpty("command_subprograms_mismatch",
                              exec_command_subprograms_mismatch(), json);

  StoreIntToJsonIfNotZero("exit", exec_exit_status(), json);
  StoreIntToJsonIfNotZero("retry", exec_request_retry(), json);
  StoreBooleanToJsonIfTrue("goma_error", goma_error(), json);
  StoreBooleanToJsonIfTrue("compiler_proxy_error", compiler_proxy_error(),
                           json);

  if (detail_level == DumpDetailLevel::kDetailed) {
    // TODO: Use absl::Time.
    if (has_start_time()) {
      (*json)["start_time"] = absl::FormatTime("%Y-%m-%d %H:%M:%S %z",
                                               absl::FromTimeT(start_time()),
                                               absl::LocalTimeZone());
    }
    StoreStringToJsonIfNotEmpty("latest_input_filename",
                                latest_input_filename(), json);
    // TODO: Use absl::Time.
    if (has_latest_input_mtime()) {
      (*json)["input_wait"] = start_time() - latest_input_mtime();
    }

    StoreIntToJsonIfNotZero("total_input", num_total_input_file(), json);
    StoreInt64ToJsonIfNotZero(
        "uploading_input", SumRepeatedInt32(num_uploading_input_file()), json);
    StoreInt64ToJsonIfNotZero(
        "missing_input", SumRepeatedInt32(num_missing_input_file()), json);

    StoreDurationToJsonIfNotZero("compiler_info_process_time",
                                 this->compiler_info_process_time, json);
    StoreDurationToJsonIfNotZero("include_preprocess_time",
                                 this->include_preprocess_time, json);

    // When depscache_used() is true, we ran include_preprocessor but its
    // processing time was 0ms. So, we'd like to show it.
    if (depscache_used() &&
        this->include_preprocess_time == absl::ZeroDuration()) {
      (*json)["include_preprocess_time"] = "0";
    }
    StoreBooleanToJsonIfTrue("depscache_used", depscache_used(), json);

    StoreDurationToJsonIfNotZero("include_fileload_time",
                                 this->include_fileload_time, json);
    StoreDurationToJsonIfNotZero("include_fileload_pending_time",
                                 this->include_fileload_pending_time, json);
    StoreDurationToJsonIfNotZero("include_fileload_run_time",
                                 this->include_fileload_run_time, json);
    StoreDurationToJsonIfNotZero("rpc_call_time", this->total_rpc_call_time,
                                 json);
    StoreDurationToJsonIfNotZero("file_response_time",
                                 this->file_response_time, json);

    StoreInt64ToJsonIfNotZero("gomacc_req_size", this->gomacc_req_size, json);
    StoreInt64ToJsonIfNotZero("gomacc_resp_size", this->gomacc_resp_size, json);
    StoreInt64ToJsonIfNotZero("exec_req_size", SumRepeatedInt32(rpc_req_size()),
                              json);
    StoreInt64ToJsonIfNotZero("exec_resp_size",
                              SumRepeatedInt32(rpc_resp_size()), json);
    StoreStringToJsonIfNotEmpty("exec_rpc_master",
                                absl::StrJoin(rpc_master_trace_id(), " "),
                                json);

    StoreDurationToJsonIfNotZero("exec_throttle_time",
                                 this->total_rpc_throttle_time, json);
    StoreDurationToJsonIfNotZero("exec_pending_time",
                                 this->total_rpc_pending_time, json);
    StoreDurationToJsonIfNotZero("exec_req_build_time",
                                 this->total_rpc_req_build_time, json);
    StoreDurationToJsonIfNotZero("exec_req_send_time",
                                 this->total_rpc_req_send_time, json);
    StoreDurationToJsonIfNotZero("exec_wait_time",
                                 this->total_rpc_wait_time, json);

    StoreDurationToJsonIfNotZero("exec_resp_recv_time",
                                 this->total_rpc_resp_recv_time, json);
    StoreDurationToJsonIfNotZero("exec_resp_parse_time",
                                 this->total_rpc_resp_parse_time, json);

    StoreStringToJsonIfNotEmpty("local_run_reason", local_run_reason(), json);
    StoreDurationToJsonIfNotZero("local_delay_time", this->local_delay_time,
                                 json);
    StoreDurationToJsonIfNotZero("local_pending_time", this->local_pending_time,
                                 json);
    StoreDurationToJsonIfNotZero("local_run_time", this->local_run_time, json);
    StoreIntToJsonIfNotZero("local_mem_kb", local_mem_kb(), json);
    StoreDurationToJsonIfNotZero("local_output_file_time",
                                 this->total_local_output_file_time, json);
    StoreInt64ToJsonIfNotZero("local_output_file_size",
                              SumRepeatedInt32(local_output_file_size()), json);

    StoreInt64ToJsonIfNotZero("output_file_size",
                              SumRepeatedInt32(output_file_size()), json);
    StoreInt64ToJsonIfNotZero("chunk_resp_size",
                              SumRepeatedInt32(chunk_resp_size()), json);
    StoreInt64ToJsonIfNotZero("output_file_rpc", this->output_file_rpc, json);

    StoreDurationToJsonIfNotZero("output_file_rpc_req_build_time",
                                 this->output_file_rpc_req_build_time, json);
    StoreDurationToJsonIfNotZero("output_file_rpc_req_send_time",
                                 this->output_file_rpc_req_send_time, json);
    StoreDurationToJsonIfNotZero("output_file_rpc_wait_time",
                                 this->output_file_rpc_wait_time, json);
    StoreDurationToJsonIfNotZero("output_file_rpc_resp_recv_time",
                                 this->output_file_rpc_resp_recv_time, json);
    StoreDurationToJsonIfNotZero("output_file_rpc_resp_parse_time",
                                 this->output_file_rpc_resp_parse_time, json);


    StoreArrayToJsonIfNotEmpty("exec_request_retry_reason",
                               exec_request_retry_reason().begin(),
                               exec_request_retry_reason().end(),
                               json);
    StoreStringToJsonIfNotEmpty("cwd", cwd(), json);
    StoreArrayToJsonIfNotEmpty("env", env().begin(), env().end(), json);
  }
}

void CompileStats::StoreStatsInExecResp(ExecResp* resp) const {
  resp->set_compiler_proxy_include_preproc_time(
      absl::ToDoubleSeconds(this->include_preprocess_time));
  resp->set_compiler_proxy_include_fileload_time(
      absl::ToDoubleSeconds(this->include_fileload_time));
  resp->set_compiler_proxy_rpc_call_time(
      absl::ToDoubleSeconds(this->total_rpc_call_time));
  resp->set_compiler_proxy_file_response_time(
      absl::ToDoubleSeconds(this->file_response_time));
  resp->set_compiler_proxy_rpc_build_time(
      absl::ToDoubleSeconds(this->total_rpc_req_build_time));
  resp->set_compiler_proxy_rpc_send_time(
      absl::ToDoubleSeconds(this->total_rpc_req_send_time));
  resp->set_compiler_proxy_rpc_wait_time(
      absl::ToDoubleSeconds(this->total_rpc_wait_time));
  resp->set_compiler_proxy_rpc_recv_time(
      absl::ToDoubleSeconds(this->total_rpc_resp_recv_time));
  resp->set_compiler_proxy_rpc_parse_time(
      absl::ToDoubleSeconds(this->total_rpc_resp_parse_time));

  resp->set_compiler_proxy_local_pending_time(
      absl::ToDoubleSeconds(this->local_pending_time));
  resp->set_compiler_proxy_local_run_time(
      absl::ToDoubleSeconds(this->local_run_time));

  resp->set_compiler_proxy_goma_error(goma_error());
  resp->set_compiler_proxy_exec_request_retry(this->exec_request_retry());
}

}  // namespace devtools_goma
