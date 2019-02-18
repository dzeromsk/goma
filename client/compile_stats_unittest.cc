// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compile_stats.h"

#include <map>

#include <json/json.h>

#include "absl/time/time.h"
#include "gtest/gtest.h"
#include "json_util.h"
#include "time_util.h"
#include "util.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

namespace {

// Returns a CompileStats object with the following fields filled in:
// - handler_time: filled with |kHandlerTime|
// - all other time fields: filled with |kOtherFieldsTime|.
CompileStats CreateStatsForTest() {
  constexpr absl::Duration kHandlerTime = absl::Milliseconds(1000);
  constexpr absl::Duration kOtherFieldsTime = absl::Milliseconds(100);

  CompileStats stats;
  stats.handler_time = kHandlerTime;

  stats.compiler_info_process_time = kOtherFieldsTime;
  stats.include_processor_wait_time = kOtherFieldsTime;
  stats.include_processor_run_time = kOtherFieldsTime;

  stats.include_fileload_time = kOtherFieldsTime;

  stats.total_rpc_req_send_time = kOtherFieldsTime;
  stats.total_rpc_resp_recv_time = kOtherFieldsTime;

  stats.file_response_time = kOtherFieldsTime;

  return stats;
}

}  // namespace

TEST(CompileStatsTest, GetMajorFactorInfoUninitialized) {
  CompileStats stats;

  EXPECT_EQ("", stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoDefaultValues) {
  auto stats = CreateStatsForTest();

  // As of this writing, when all factor time values are the same, the major
  // factor will be the first one that is compared. Instead of worrying about
  // which one comes first, just make sure that it is not empty.
  EXPECT_NE("", stats.GetMajorFactorInfo());
}

// TODO: Convert remaining int32 time fields to absl::Duration.
TEST(CompileStatsTest, GetMajorFactorInfoZeroHandlerTime) {
  auto stats = CreateStatsForTest();
  stats.compiler_info_process_time = absl::Milliseconds(200);
  stats.handler_time = absl::ZeroDuration();

  EXPECT_EQ("compiler_info: 200 ms", stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoCompilerInfoProcessTime) {
  auto stats = CreateStatsForTest();
  stats.compiler_info_process_time = absl::Milliseconds(200);

  EXPECT_EQ("compiler_info: 200 ms [20%]", stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoIncludeProcessorWaitTime) {
  auto stats = CreateStatsForTest();
  stats.include_processor_wait_time = absl::Milliseconds(250);

  EXPECT_EQ("include_processor_wait_time: 250 ms [25%]",
            stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoIncludeProcessorRunTime) {
  auto stats = CreateStatsForTest();
  stats.include_processor_run_time = absl::Milliseconds(300);

  EXPECT_EQ("include_processor_run_time: 300 ms [30%]",
            stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoIncludeFileloadTime) {
  auto stats = CreateStatsForTest();
  stats.include_fileload_time = absl::Milliseconds(150);

  EXPECT_EQ("file_upload: 150 ms [15%]", stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoTotalRPCReqSendTime) {
  auto stats = CreateStatsForTest();
  stats.total_rpc_req_send_time = absl::Milliseconds(120);

  EXPECT_EQ("rpc_req: 120 ms [12%]", stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoTotalRPCRespRecvTime) {
  auto stats = CreateStatsForTest();
  stats.total_rpc_resp_recv_time = absl::Milliseconds(350);

  EXPECT_EQ("rpc_resp: 350 ms [35%]", stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, GetMajorFactorInfoFileResponseTime) {
  auto stats = CreateStatsForTest();
  stats.file_response_time = absl::Milliseconds(360);

  EXPECT_EQ("file_download: 360 ms [36%]", stats.GetMajorFactorInfo());
}

TEST(CompileStatsTest, AddStatsFromHttpStatusMasterTraceIdOnly) {
  HttpClient::Status status;
  status.master_trace_id = "master trace";
  status.trace_id = "trace";
  status.req_size = 1;
  status.resp_size = 2;
  status.raw_req_size = 3;
  status.raw_resp_size = 4;

  CompileStats stats;
  stats.AddStatsFromHttpStatus(status);

  ASSERT_EQ(1, stats.rpc_master_trace_id().size());
  EXPECT_EQ("master trace", stats.rpc_master_trace_id()[0]);

  // Make sure no size fields were added.
  EXPECT_EQ(stats.rpc_req_size().size(), 0);
  EXPECT_EQ(stats.rpc_resp_size().size(), 0);
  EXPECT_EQ(stats.rpc_raw_req_size().size(), 0);
  EXPECT_EQ(stats.rpc_raw_resp_size().size(), 0);
}

TEST(CompileStatsTest, AddStatsFromHttpStatusMatchingTraceId) {
  HttpClient::Status status;
  status.master_trace_id = "master trace";
  status.trace_id = "master trace";
  status.req_size = 1;
  status.resp_size = 2;
  status.raw_req_size = 3;
  status.raw_resp_size = 4;

  CompileStats stats;
  stats.AddStatsFromHttpStatus(status);

  ASSERT_EQ(1, stats.rpc_master_trace_id().size());
  EXPECT_EQ("master trace", stats.rpc_master_trace_id()[0]);

  ASSERT_EQ(stats.rpc_req_size().size(), 1);
  ASSERT_EQ(stats.rpc_resp_size().size(), 1);
  ASSERT_EQ(stats.rpc_raw_req_size().size(), 1);
  ASSERT_EQ(stats.rpc_raw_resp_size().size(), 1);

  EXPECT_EQ(1, stats.rpc_req_size()[0]);
  EXPECT_EQ(2, stats.rpc_resp_size()[0]);
  EXPECT_EQ(3, stats.rpc_raw_req_size()[0]);
  EXPECT_EQ(4, stats.rpc_raw_resp_size()[0]);
}

TEST(CompileStatsTest, AddStatsFromHttpStatusTimesSingle) {
  HttpClient::Status status;
  status.throttle_time = absl::Milliseconds(100);
  status.pending_time = absl::Milliseconds(120);
  status.req_build_time = absl::Milliseconds(140);
  status.req_send_time = absl::Milliseconds(160);
  status.wait_time = absl::Milliseconds(180);
  status.resp_recv_time = absl::Milliseconds(200);
  status.resp_parse_time = absl::Milliseconds(220);

  CompileStats stats;
  stats.AddStatsFromHttpStatus(status);

  ASSERT_EQ(stats.rpc_throttle_time().size(), 1);
  ASSERT_EQ(stats.rpc_pending_time().size(), 1);
  ASSERT_EQ(stats.rpc_req_build_time().size(), 1);
  ASSERT_EQ(stats.rpc_req_send_time().size(), 1);
  ASSERT_EQ(stats.rpc_wait_time().size(), 1);
  ASSERT_EQ(stats.rpc_resp_recv_time().size(), 1);
  ASSERT_EQ(stats.rpc_resp_parse_time().size(), 1);

  EXPECT_EQ(100, stats.rpc_throttle_time()[0]);
  EXPECT_EQ(120, stats.rpc_pending_time()[0]);
  EXPECT_EQ(140, stats.rpc_req_build_time()[0]);
  EXPECT_EQ(160, stats.rpc_req_send_time()[0]);
  EXPECT_EQ(180, stats.rpc_wait_time()[0]);
  EXPECT_EQ(200, stats.rpc_resp_recv_time()[0]);
  EXPECT_EQ(220, stats.rpc_resp_parse_time()[0]);

  EXPECT_EQ(absl::Milliseconds(100), stats.total_rpc_throttle_time);
  EXPECT_EQ(absl::Milliseconds(120), stats.total_rpc_pending_time);
  EXPECT_EQ(absl::Milliseconds(140), stats.total_rpc_req_build_time);
  EXPECT_EQ(absl::Milliseconds(160), stats.total_rpc_req_send_time);
  EXPECT_EQ(absl::Milliseconds(180), stats.total_rpc_wait_time);
  EXPECT_EQ(absl::Milliseconds(200), stats.total_rpc_resp_recv_time);
  EXPECT_EQ(absl::Milliseconds(220), stats.total_rpc_resp_parse_time);
}

TEST(CompileStatsTest, AddStatsFromHttpStatusTimesMultiple) {
  HttpClient::Status status1;
  status1.throttle_time = absl::Milliseconds(100);
  status1.pending_time = absl::Milliseconds(120);
  status1.req_build_time = absl::Milliseconds(140);
  status1.req_send_time = absl::Milliseconds(160);
  status1.wait_time = absl::Milliseconds(180);
  status1.resp_recv_time = absl::Milliseconds(200);
  status1.resp_parse_time = absl::Milliseconds(220);

  HttpClient::Status status2;
  status2.throttle_time = absl::Milliseconds(300);
  status2.pending_time = absl::Milliseconds(320);
  status2.req_build_time = absl::Milliseconds(340);
  status2.req_send_time = absl::Milliseconds(360);
  status2.wait_time = absl::Milliseconds(380);
  status2.resp_recv_time = absl::Milliseconds(400);
  status2.resp_parse_time = absl::Milliseconds(420);

  CompileStats stats;
  stats.AddStatsFromHttpStatus(status1);
  stats.AddStatsFromHttpStatus(status2);

  ASSERT_EQ(stats.rpc_throttle_time().size(), 2);
  ASSERT_EQ(stats.rpc_pending_time().size(), 2);
  ASSERT_EQ(stats.rpc_req_build_time().size(), 2);
  ASSERT_EQ(stats.rpc_req_send_time().size(), 2);
  ASSERT_EQ(stats.rpc_wait_time().size(), 2);
  ASSERT_EQ(stats.rpc_resp_recv_time().size(), 2);
  ASSERT_EQ(stats.rpc_resp_parse_time().size(), 2);

  EXPECT_EQ(100, stats.rpc_throttle_time()[0]);
  EXPECT_EQ(120, stats.rpc_pending_time()[0]);
  EXPECT_EQ(140, stats.rpc_req_build_time()[0]);
  EXPECT_EQ(160, stats.rpc_req_send_time()[0]);
  EXPECT_EQ(180, stats.rpc_wait_time()[0]);
  EXPECT_EQ(200, stats.rpc_resp_recv_time()[0]);
  EXPECT_EQ(220, stats.rpc_resp_parse_time()[0]);

  EXPECT_EQ(300, stats.rpc_throttle_time()[1]);
  EXPECT_EQ(320, stats.rpc_pending_time()[1]);
  EXPECT_EQ(340, stats.rpc_req_build_time()[1]);
  EXPECT_EQ(360, stats.rpc_req_send_time()[1]);
  EXPECT_EQ(380, stats.rpc_wait_time()[1]);
  EXPECT_EQ(400, stats.rpc_resp_recv_time()[1]);
  EXPECT_EQ(420, stats.rpc_resp_parse_time()[1]);

  EXPECT_EQ(absl::Milliseconds(400), stats.total_rpc_throttle_time);
  EXPECT_EQ(absl::Milliseconds(440), stats.total_rpc_pending_time);
  EXPECT_EQ(absl::Milliseconds(480), stats.total_rpc_req_build_time);
  EXPECT_EQ(absl::Milliseconds(520), stats.total_rpc_req_send_time);
  EXPECT_EQ(absl::Milliseconds(560), stats.total_rpc_wait_time);
  EXPECT_EQ(absl::Milliseconds(600), stats.total_rpc_resp_recv_time);
  EXPECT_EQ(absl::Milliseconds(640), stats.total_rpc_resp_parse_time);
}



TEST(CompileStatsTest, DumpToJsonEmpty) {
  CompileStats stats;

  // Make sure that all fields are default-initialized to the values that are
  // not dumped to JSON (ZeroDuration(), "", 0, etc).
  EXPECT_EQ(absl::ZeroDuration(), stats.handler_time);
  EXPECT_EQ(absl::ZeroDuration(), stats.include_processor_wait_time);

  EXPECT_EQ("", stats.exec_command_version_mismatch());
  EXPECT_EQ("", stats.exec_command_binary_hash_mismatch());
  EXPECT_EQ("", stats.exec_command_subprograms_mismatch());

  EXPECT_EQ(0, stats.exec_exit_status());
  EXPECT_EQ(0, stats.exec_request_retry());
  EXPECT_EQ(0, stats.goma_error());
  EXPECT_EQ(0, stats.compiler_proxy_error());

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  EXPECT_EQ(0, json.getMemberNames().size()) << json.toStyledString();
}

TEST(CompileStatsTest, DumpToJsonBasic) {
  CompileStats stats = CreateStatsForTest();

  stats.handler_time = absl::Milliseconds(1400);
  stats.include_processor_wait_time = absl::Milliseconds(308);

  stats.set_exec_command_version_mismatch("command version mismatch");
  stats.set_exec_command_binary_hash_mismatch("command binary hash mismatch");
  stats.set_exec_command_subprograms_mismatch("command subprograms mismatch");

  stats.set_exec_exit_status(10);
  stats.set_exec_request_retry(20);
  // These two are boolean fields. Explicitly set them to values >1 to test that
  // booleans are properly handled and dumped as "1" if set.
  stats.set_goma_error(100);
  stats.set_compiler_proxy_error(200);

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kNotDetailed);

  EXPECT_EQ(9, json.getMemberNames().size()) << json.toStyledString();

  EXPECT_TRUE(json.isMember("duration"));
  EXPECT_TRUE(json.isMember("major_factor"));

  EXPECT_TRUE(json.isMember("command_version_mismatch"));
  EXPECT_TRUE(json.isMember("command_binary_hash_mismatch"));
  EXPECT_TRUE(json.isMember("command_subprograms_mismatch"));

  EXPECT_TRUE(json.isMember("exit"));
  EXPECT_TRUE(json.isMember("retry"));
  EXPECT_TRUE(json.isMember("goma_error"));
  EXPECT_TRUE(json.isMember("compiler_proxy_error"));

  std::string error_message;

  std::string time_str;
  EXPECT_TRUE(GetStringFromJson(json, "duration", &time_str, &error_message))
      << error_message;
  EXPECT_EQ("1400 ms", time_str);

  std::string major_factor_info;
  EXPECT_TRUE(GetStringFromJson(json, "major_factor", &major_factor_info,
                                &error_message)) << error_message;
  EXPECT_EQ("include_processor_wait_time: 308 ms [22%]", major_factor_info);

  std::string command_version_mismatch;
  EXPECT_TRUE(GetStringFromJson(json, "command_version_mismatch",
                                &command_version_mismatch,
                                &error_message)) << error_message;
  EXPECT_EQ("command version mismatch", command_version_mismatch);

  std::string command_binary_hash_mismatch;
  EXPECT_TRUE(GetStringFromJson(json, "command_binary_hash_mismatch",
                                &command_binary_hash_mismatch,
                                &error_message)) << error_message;
  EXPECT_EQ("command binary hash mismatch", command_binary_hash_mismatch);

  int exit_status = -1;
  EXPECT_TRUE(GetIntFromJson(json, "exit", &exit_status, &error_message))
      << error_message;
  EXPECT_EQ(10, exit_status);

  int num_retries = -1;
  EXPECT_TRUE(GetIntFromJson(json, "retry", &num_retries,
                             &error_message)) << error_message;
  EXPECT_EQ(20, num_retries);

  std::string goma_error;
  EXPECT_TRUE(GetStringFromJson(json, "goma_error", &goma_error,
                                &error_message)) << error_message;
  EXPECT_EQ("true", goma_error);

  std::string compiler_proxy_error;
  EXPECT_TRUE(GetStringFromJson(json, "compiler_proxy_error",
                                &compiler_proxy_error,
                                &error_message)) << error_message;
  EXPECT_EQ("true", compiler_proxy_error);
}

TEST(CompileStatsTest, DumpToJsonCacheHit) {
  CompileStats stats_cache_hit;
  stats_cache_hit.set_cache_hit(true);
  stats_cache_hit.set_cache_source(ExecLog::STORAGE_CACHE);

  Json::Value json;
  stats_cache_hit.DumpToJson(&json,
                             CompileStats::DumpDetailLevel::kNotDetailed);

  std::string error_message;

  std::string cache_str;
  EXPECT_TRUE(GetStringFromJson(json, "cache", &cache_str, &error_message))
      << error_message;
  EXPECT_EQ("hit", cache_str);
}


TEST(CompileStatsTest, DumpToJsonLocalCacheHit) {
  CompileStats stats_local_cache_hit;
  stats_local_cache_hit.set_cache_hit(true);
  stats_local_cache_hit.set_cache_source(ExecLog::LOCAL_OUTPUT_CACHE);

  Json::Value json;
  stats_local_cache_hit.DumpToJson(&json,
                                   CompileStats::DumpDetailLevel::kNotDetailed);

  std::string error_message;

  std::string cache_str;
  EXPECT_TRUE(GetStringFromJson(json, "cache", &cache_str, &error_message))
      << error_message;
  EXPECT_EQ("local hit", cache_str);
}

TEST(CompileStatsTest, DumpToJsonNoCacheHit) {
  CompileStats stats_no_cache_hit;
  stats_no_cache_hit.set_cache_hit(false);
  stats_no_cache_hit.set_cache_source(ExecLog::MEM_CACHE);

  Json::Value json;
  stats_no_cache_hit.DumpToJson(&json,
                                CompileStats::DumpDetailLevel::kNotDetailed);

  EXPECT_EQ(0, json.getMemberNames().size()) << json.toStyledString();
}

TEST(CompileStatsTest, DumpToJsonDetailedStartStats) {
  CompileStats stats;

  stats.set_start_time(60);  // One minute after Unix Epoch.
  stats.set_latest_input_filename("foo.cc");
  stats.set_latest_input_mtime(30);  // 30 sec after Unix Epoch.
  stats.set_num_total_input_file(250);

  stats.add_num_uploading_input_file(20);
  stats.add_num_uploading_input_file(100);
  stats.add_num_uploading_input_file(120);

  stats.add_num_missing_input_file(5);
  stats.add_num_missing_input_file(13);

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  EXPECT_EQ(6, json.getMemberNames().size()) << json.toStyledString();

  std::string error_message;

  std::string start_time_str;
  EXPECT_TRUE(GetStringFromJson(json, "start_time", &start_time_str,
                                &error_message)) << error_message;
  // The actual timestamp string is dependent on the current time zone. Just
  // make sure that it is a non-empty string.
  EXPECT_NE("", start_time_str);

  std::string latest_input_filename;
  EXPECT_TRUE(GetStringFromJson(json, "latest_input_filename",
                                &latest_input_filename,
                                &error_message)) << error_message;
  EXPECT_EQ("foo.cc", latest_input_filename);

  int input_wait_sec = -1;
  EXPECT_TRUE(GetIntFromJson(json, "input_wait", &input_wait_sec,
                             &error_message)) << error_message;
  EXPECT_EQ(30, input_wait_sec);

  int total_input_files = -1;
  EXPECT_TRUE(GetIntFromJson(json, "total_input", &total_input_files,
                             &error_message)) << error_message;
  EXPECT_EQ(250, total_input_files);

  int64_t num_uploading_input_files = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "uploading_input",
                               &num_uploading_input_files,
                               &error_message)) << error_message;
  EXPECT_EQ(240, num_uploading_input_files);

  int64_t num_missing_input_files = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "missing_input", &num_missing_input_files,
                               &error_message)) << error_message;
  EXPECT_EQ(18, num_missing_input_files);
}

TEST(CompileStatsTest, DumpToJsonDetailedRpcExecStats) {
  CompileStats stats;
  stats.gomacc_req_size = 35e9L;
  stats.gomacc_resp_size = 65e9L;

  stats.add_rpc_req_size(36000);
  stats.add_rpc_req_size(47000);
  stats.add_rpc_req_size(33000);

  stats.add_rpc_resp_size(166000);
  stats.add_rpc_resp_size(99000);
  stats.add_rpc_resp_size(1000);

  stats.add_rpc_master_trace_id("hello");
  stats.add_rpc_master_trace_id("goodbye");
  stats.add_rpc_master_trace_id("thanks");

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  EXPECT_EQ(5, json.getMemberNames().size()) << json.toStyledString();

  std::string error_message;

  int64_t gomacc_req_size = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "gomacc_req_size", &gomacc_req_size,
                               &error_message)) << error_message;
  EXPECT_EQ(35e9L, gomacc_req_size);

  int64_t gomacc_resp_size = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "gomacc_resp_size", &gomacc_resp_size,
                               &error_message)) << error_message;
  EXPECT_EQ(65e9L, gomacc_resp_size);

  int64_t rpc_req_size = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "exec_req_size", &rpc_req_size,
                               &error_message)) << error_message;
  EXPECT_EQ(116000, rpc_req_size);

  int64_t rpc_resp_size = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "exec_resp_size", &rpc_resp_size,
                               &error_message)) << error_message;
  EXPECT_EQ(266000, rpc_resp_size);

  std::string exec_rpc_master;
  EXPECT_TRUE(GetStringFromJson(json, "exec_rpc_master", &exec_rpc_master,
                                &error_message)) << error_message;
  EXPECT_EQ("hello goodbye thanks", exec_rpc_master);
}

TEST(CompileStatsTest, DumpToJsonDetailedDurations) {
  CompileStats stats;

  stats.compiler_info_process_time = absl::Milliseconds(100);
  stats.include_preprocess_time = absl::Milliseconds(150);

  stats.include_fileload_time = absl::Milliseconds(200);
  stats.include_fileload_pending_time = absl::Milliseconds(300);
  stats.include_fileload_run_time = absl::Milliseconds(400);
  stats.total_rpc_call_time = absl::Milliseconds(500);
  stats.file_response_time = absl::Milliseconds(600);

  stats.total_rpc_throttle_time = absl::Milliseconds(700);
  stats.total_rpc_pending_time = absl::Milliseconds(800);
  stats.total_rpc_req_build_time = absl::Milliseconds(900);
  stats.total_rpc_req_send_time = absl::Milliseconds(1000);
  stats.total_rpc_wait_time = absl::Milliseconds(1100);

  stats.total_rpc_resp_recv_time = absl::Milliseconds(1200);
  stats.total_rpc_resp_parse_time = absl::Milliseconds(1300);

  stats.output_file_rpc_req_build_time = absl::Milliseconds(1400);
  stats.output_file_rpc_req_send_time = absl::Milliseconds(1500);
  stats.output_file_rpc_wait_time = absl::Milliseconds(1600);
  stats.output_file_rpc_resp_recv_time = absl::Milliseconds(1700);
  stats.output_file_rpc_resp_parse_time = absl::Milliseconds(1800);

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  // "major_factor" is an extra field that is not explicitly set.
  EXPECT_EQ(20, json.getMemberNames().size()) << json.toStyledString();
  EXPECT_TRUE(json.isMember("major_factor"));

  // Extract string value for each key and store it in |json_string_values|.
  // This code is cleaner than calling EXPECT_TRUE(GetStringFromJson()) sixteen
  // times and checking the value each time manually.
  const char* kJsonKeys[] = {
    "compiler_info_process_time",
    "include_preprocess_time",
    "include_fileload_time",
    "include_fileload_pending_time",
    "include_fileload_run_time",
    "rpc_call_time",
    "file_response_time",
    "exec_throttle_time",
    "exec_pending_time",
    "exec_req_build_time",
    "exec_req_send_time",
    "exec_wait_time",
    "exec_resp_recv_time",
    "exec_resp_parse_time",
    "output_file_rpc_req_build_time",
    "output_file_rpc_req_send_time",
    "output_file_rpc_wait_time",
    "output_file_rpc_resp_recv_time",
    "output_file_rpc_resp_parse_time",
  };
  std::map<std::string, std::string> json_string_values;
  for (const char* key : kJsonKeys) {
    std::string string_value, error_message;
    EXPECT_TRUE(GetStringFromJson(json, key, &string_value, &error_message))
        << error_message;

    json_string_values[key] = string_value;
  }

  EXPECT_EQ("100 ms", json_string_values["compiler_info_process_time"]);
  EXPECT_EQ("150 ms", json_string_values["include_preprocess_time"]);
  EXPECT_EQ("200 ms", json_string_values["include_fileload_time"]);
  EXPECT_EQ("300 ms", json_string_values["include_fileload_pending_time"]);
  EXPECT_EQ("400 ms", json_string_values["include_fileload_run_time"]);
  EXPECT_EQ("500 ms", json_string_values["rpc_call_time"]);
  EXPECT_EQ("600 ms", json_string_values["file_response_time"]);
  EXPECT_EQ("700 ms", json_string_values["exec_throttle_time"]);
  EXPECT_EQ("800 ms", json_string_values["exec_pending_time"]);
  EXPECT_EQ("900 ms", json_string_values["exec_req_build_time"]);
  EXPECT_EQ("1 s", json_string_values["exec_req_send_time"]);
  EXPECT_EQ("1.1 s", json_string_values["exec_wait_time"]);
  EXPECT_EQ("1.2 s", json_string_values["exec_resp_recv_time"]);
  EXPECT_EQ("1.3 s", json_string_values["exec_resp_parse_time"]);
  EXPECT_EQ("1.4 s", json_string_values["output_file_rpc_req_build_time"]);
  EXPECT_EQ("1.5 s", json_string_values["output_file_rpc_req_send_time"]);
  EXPECT_EQ("1.6 s", json_string_values["output_file_rpc_wait_time"]);
  EXPECT_EQ("1.7 s", json_string_values["output_file_rpc_resp_recv_time"]);
  EXPECT_EQ("1.8 s", json_string_values["output_file_rpc_resp_parse_time"]);
}

TEST(CompileStatsTest, DumpToJsonDepsCacheUsed) {
  CompileStats stats;
  stats.set_depscache_used(true);

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  std::string error_message;

  std::string depscache_used;
  EXPECT_TRUE(GetStringFromJson(json, "depscache_used", &depscache_used,
                                &error_message)) << error_message;
  EXPECT_EQ("true", depscache_used);

  std::string include_preprocess_time;
  EXPECT_TRUE(GetStringFromJson(json, "include_preprocess_time",
                                &include_preprocess_time,
                                &error_message)) << error_message;
  EXPECT_EQ("0", include_preprocess_time);
}

TEST(CompileStatsTest, DumpToJsonLocalRunStats) {
  CompileStats stats;
  stats.set_local_run_reason("foobar");
  stats.set_local_mem_kb(999);

  stats.add_local_output_file_size(1000000);
  stats.add_local_output_file_size(1500000);
  stats.add_local_output_file_size(3500000);

  stats.local_delay_time = absl::Milliseconds(1250);
  stats.local_pending_time = absl::Milliseconds(3450);
  stats.local_run_time = absl::Milliseconds(5650);
  stats.total_local_output_file_time = absl::Milliseconds(1100);

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  std::string error_message;

  std::string local_run_reason;
  EXPECT_TRUE(GetStringFromJson(json, "local_run_reason", &local_run_reason,
                                &error_message)) << error_message;
  EXPECT_EQ("foobar", local_run_reason);

  std::string local_delay_time;
  EXPECT_TRUE(GetStringFromJson(json, "local_delay_time", &local_delay_time,
                                &error_message))
      << error_message;
  EXPECT_EQ("1.25 s", local_delay_time);

  std::string local_pending_time;
  EXPECT_TRUE(GetStringFromJson(json, "local_pending_time", &local_pending_time,
                                &error_message))
      << error_message;
  EXPECT_EQ("3.45 s", local_pending_time);

  std::string local_run_time;
  EXPECT_TRUE(GetStringFromJson(json, "local_run_time", &local_run_time,
                                &error_message))
      << error_message;
  EXPECT_EQ("5.65 s", local_run_time);

  int64_t local_mem_kb = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "local_mem_kb", &local_mem_kb,
                               &error_message)) << error_message;
  EXPECT_EQ(999, local_mem_kb);

  std::string local_output_file_time;
  EXPECT_TRUE(GetStringFromJson(json, "local_output_file_time",
                                &local_output_file_time, &error_message))
      << error_message;
  EXPECT_EQ("1.1 s", local_output_file_time);

  int64_t local_output_file_size = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "local_output_file_size",
                               &local_output_file_size,
                               &error_message)) << error_message;
  EXPECT_EQ(6000000, local_output_file_size);
}

TEST(CompileStatsTest, DumpToJsonOutputFileStats) {
  CompileStats stats;

  stats.add_output_file_size(10000);
  stats.add_output_file_size(20000);
  stats.add_output_file_size(40000);
  stats.add_output_file_size(80000);

  stats.add_chunk_resp_size(3000);
  stats.add_chunk_resp_size(5000);
  stats.add_chunk_resp_size(11000);
  stats.add_chunk_resp_size(9000);

  stats.output_file_rpc = 5;

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  EXPECT_EQ(3, json.getMemberNames().size()) << json.toStyledString();

  std::string error_message;

  int64_t output_file_size = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "output_file_size", &output_file_size,
                               &error_message)) << error_message;
  EXPECT_EQ(150000, output_file_size);

  int64_t chunk_resp_size = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "chunk_resp_size", &chunk_resp_size,
                               &error_message)) << error_message;
  EXPECT_EQ(28000, chunk_resp_size);

  int64_t num_output_file_rpc = -1;
  EXPECT_TRUE(GetInt64FromJson(json, "output_file_rpc", &num_output_file_rpc,
                               &error_message)) << error_message;
  EXPECT_EQ(5, num_output_file_rpc);
}


TEST(CompileStatsTest, DumpToJsonEndStats) {
  CompileStats stats;

  stats.add_exec_request_retry_reason("the");
  stats.add_exec_request_retry_reason("quick");
  stats.add_exec_request_retry_reason("brown");
  stats.add_exec_request_retry_reason("fox");

  stats.add_env("jumps");
  stats.add_env("over");
  stats.add_env("the");
  stats.add_env("lazy");
  stats.add_env("dog");

  stats.set_cwd("/dev/null");

  Json::Value json;
  stats.DumpToJson(&json, CompileStats::DumpDetailLevel::kDetailed);

  EXPECT_EQ(3, json.getMemberNames().size()) << json.toStyledString();

  std::string error_message;

  Json::Value retry_reason_array;
  EXPECT_TRUE(GetArrayFromJson(json, "exec_request_retry_reason",
                               &retry_reason_array,
                               &error_message)) << error_message;
  ASSERT_EQ(4, retry_reason_array.size());
  EXPECT_TRUE(retry_reason_array[0].isString());
  EXPECT_TRUE(retry_reason_array[1].isString());
  EXPECT_TRUE(retry_reason_array[2].isString());
  EXPECT_TRUE(retry_reason_array[3].isString());
  EXPECT_EQ("the", retry_reason_array[0].asString());
  EXPECT_EQ("quick", retry_reason_array[1].asString());
  EXPECT_EQ("brown", retry_reason_array[2].asString());
  EXPECT_EQ("fox", retry_reason_array[3].asString());

  Json::Value env_array;
  EXPECT_TRUE(GetArrayFromJson(json, "env", &env_array, &error_message))
      << error_message;
  ASSERT_EQ(5, env_array.size());
  EXPECT_TRUE(env_array[0].isString());
  EXPECT_TRUE(env_array[1].isString());
  EXPECT_TRUE(env_array[2].isString());
  EXPECT_TRUE(env_array[3].isString());
  EXPECT_TRUE(env_array[4].isString());
  EXPECT_EQ("jumps", env_array[0].asString());
  EXPECT_EQ("over", env_array[1].asString());
  EXPECT_EQ("the", env_array[2].asString());
  EXPECT_EQ("lazy", env_array[3].asString());
  EXPECT_EQ("dog", env_array[4].asString());

  std::string cwd;
  EXPECT_TRUE(GetStringFromJson(json, "cwd", &cwd, &error_message))
      << error_message;
  EXPECT_EQ("/dev/null", cwd);
}

TEST(CompileStatsTest, StoreStatsInExecRespEmpty) {
  CompileStats stats;

  ExecResp resp;
  stats.StoreStatsInExecResp(&resp);

  EXPECT_EQ(0, resp.compiler_proxy_include_preproc_time());
  EXPECT_EQ(0, resp.compiler_proxy_include_fileload_time());
  EXPECT_EQ(0, resp.compiler_proxy_rpc_call_time());
  EXPECT_EQ(0, resp.compiler_proxy_file_response_time());
  EXPECT_EQ(0, resp.compiler_proxy_rpc_build_time());
  EXPECT_EQ(0, resp.compiler_proxy_rpc_send_time());
  EXPECT_EQ(0, resp.compiler_proxy_rpc_wait_time());
  EXPECT_EQ(0, resp.compiler_proxy_rpc_recv_time());
  EXPECT_EQ(0, resp.compiler_proxy_rpc_parse_time());
  EXPECT_EQ(0, resp.compiler_proxy_local_pending_time());
  EXPECT_EQ(0, resp.compiler_proxy_local_run_time());

  EXPECT_FALSE(resp.compiler_proxy_goma_error());
  EXPECT_EQ(0, resp.compiler_proxy_exec_request_retry());
}

TEST(CompileStatsTest, StoreStatsInExecRespNonEmpty) {
  CompileStats stats;

  stats.include_preprocess_time = absl::Milliseconds(125);
  stats.include_fileload_time = absl::Milliseconds(250);
  stats.total_rpc_call_time = absl::Milliseconds(500);
  stats.file_response_time = absl::Milliseconds(625);
  stats.total_rpc_req_build_time = absl::Milliseconds(875);
  stats.total_rpc_req_send_time = absl::Milliseconds(1000);
  stats.total_rpc_wait_time = absl::Milliseconds(1125);
  stats.total_rpc_resp_recv_time = absl::Milliseconds(1250);
  stats.total_rpc_resp_parse_time = absl::Milliseconds(1375);
  stats.local_pending_time = absl::Milliseconds(1500);
  stats.local_run_time = absl::Milliseconds(1625);

  stats.set_goma_error(true);
  stats.set_exec_request_retry(44);

  ExecResp resp;
  stats.StoreStatsInExecResp(&resp);

  EXPECT_EQ(0.125, resp.compiler_proxy_include_preproc_time());
  EXPECT_EQ(0.25, resp.compiler_proxy_include_fileload_time());
  EXPECT_EQ(0.5, resp.compiler_proxy_rpc_call_time());
  EXPECT_EQ(0.625, resp.compiler_proxy_file_response_time());
  EXPECT_EQ(0.875, resp.compiler_proxy_rpc_build_time());
  EXPECT_EQ(1.0, resp.compiler_proxy_rpc_send_time());
  EXPECT_EQ(1.125, resp.compiler_proxy_rpc_wait_time());
  EXPECT_EQ(1.250, resp.compiler_proxy_rpc_recv_time());
  EXPECT_EQ(1.375, resp.compiler_proxy_rpc_parse_time());
  EXPECT_EQ(1.5, resp.compiler_proxy_local_pending_time());
  EXPECT_EQ(1.625, resp.compiler_proxy_local_run_time());

  EXPECT_TRUE(resp.compiler_proxy_goma_error());
  EXPECT_EQ(44, resp.compiler_proxy_exec_request_retry());
}

}  // namespace devtools_goma
