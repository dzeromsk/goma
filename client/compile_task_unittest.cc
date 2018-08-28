// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compile_task.h"

#include <memory>
#include <set>
#include <string>

#include <json/json.h>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "callback.h"
#include "compile_stats.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "gtest/gtest.h"
#include "json_util.h"
#include "prototmp/goma_data.pb.h"
#include "threadpool_http_server.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

namespace {

constexpr int kCompileTaskId = 1234;

class DummyHttpHandler : public ThreadpoolHttpServer::HttpHandler {
 public:
  ~DummyHttpHandler() override = default;

  void HandleHttpRequest(
      ThreadpoolHttpServer::HttpServerRequest* http_server_request) override {}

  bool shutting_down() override { return true; }
};

class DummyHttpServerRequest : public ThreadpoolHttpServer::HttpServerRequest {
 public:
  class DummyMonitor : public ThreadpoolHttpServer::Monitor {
   public:
    ~DummyMonitor() override = default;

    void FinishHandle(const ThreadpoolHttpServer::Stat& stat) override {}
  };

  DummyHttpServerRequest(WorkerThreadManager* worker_thread_manager,
                         ThreadpoolHttpServer* http_server)
      : ThreadpoolHttpServer::HttpServerRequest(worker_thread_manager,
                                                http_server,
                                                stat_,
                                                &monitor_) {}

  bool CheckCredential() override { return true; }

  bool IsTrusted() override { return true; }

  void SendReply(const string& response) override {}

  void NotifyWhenClosed(OneshotClosure* callback) override { delete callback; }

 private:
  ThreadpoolHttpServer::Stat stat_;
  DummyMonitor monitor_;
};

// ExecServiceClient used for testing. Just returns the HTTP response code that
// is provided to the constructor.
class FakeExecServiceClient : public ExecServiceClient {
 public:
  explicit FakeExecServiceClient(int http_return_code)
      : ExecServiceClient(nullptr, ""),
        http_return_code_(http_return_code) {}
  ~FakeExecServiceClient() override = default;

  void ExecAsync(const ExecReq* req, ExecResp* resp,
                 HttpClient::Status* status,
                 OneshotClosure* callback) override {
    status->http_return_code = http_return_code_;
    callback->Run();
  }

 private:
  int http_return_code_;
};

// Unit tests that require a real instance of CompileTask should inherit from
// this class.
class CompileTaskTest : public ::testing::Test {
 public:
  void SetUp() override {
    // This is required for ProcessCallExec() to pass.
    exec_request_.add_input();

    // Create a task that does not get executed. The values and objects that are
    // passed in are dummy values.
    worker_thread_manager_ = absl::make_unique<WorkerThreadManager>();
    http_server_ =
        absl::make_unique<ThreadpoolHttpServer>("LISTEN_ADDR", 8088, 1,
                                                worker_thread_manager_.get(), 1,
                                                &http_handler_, 3);
    compile_service_ =
        absl::make_unique<CompileService>(worker_thread_manager_.get(), 1);

    http_server_request_ = absl::make_unique<DummyHttpServerRequest>(
        worker_thread_manager_.get(), http_server_.get());
    rpc_controller_ = absl::make_unique<CompileService::RpcController>(
        http_server_request_.get());

    compile_task_ = new CompileTask(compile_service_.get(), kCompileTaskId);
    compile_task_->Init(rpc_controller_.get(), exec_request_, &exec_response_,
                        nullptr);
  }

  void TearDown() override {
    // Make sure all CHECKs pass by signaling the end of a CompileTask.
    rpc_controller_->SendReply(exec_response_);
    worker_thread_manager_->Finish();
  }

  // Clean up |*compile_task_| if it was not cleaned up automatically.
  void ManuallyCleanUpCompileTask() {
    // TODO: CompileTask/CompileService should be refactored so that it
    // does not have to be manually deallocated.
    compile_task_->Deref();
    compile_task_ = nullptr;
  }

  CompileTask* compile_task() const { return compile_task_; }
  const std::unique_ptr<CompileService>& compile_service() const {
    return compile_service_;
  }

 private:
  // These objects need to be initialized at the start of each test.
  std::unique_ptr<WorkerThreadManager> worker_thread_manager_;
  std::unique_ptr<ThreadpoolHttpServer> http_server_;
  std::unique_ptr<CompileService> compile_service_;
  std::unique_ptr<DummyHttpServerRequest> http_server_request_;
  std::unique_ptr<CompileService::RpcController> rpc_controller_;

  // CompileTask's destructor is private. It must be a bare pointer. Call
  // Deref() to clean up.
  CompileTask* compile_task_ = nullptr;

  // These are objects whose contents are not used. They do not need to be
  // re-initialized at the start of each test.
  ExecReq exec_request_;
  ExecResp exec_response_;
  DummyHttpHandler http_handler_;
};

}  // anonymous namespace

TEST_F(CompileTaskTest, DumpToJsonWithoutRunning) {
  Json::Value json;
  compile_task()->DumpToJson(false, &json);

  EXPECT_EQ(4, json.getMemberNames().size()) << json.toStyledString();
  EXPECT_TRUE(json.isMember("elapsed"));
  EXPECT_TRUE(json.isMember("id"));
  EXPECT_TRUE(json.isMember("state"));
  EXPECT_TRUE(json.isMember("summaryOnly"));

  EXPECT_FALSE(json.isMember("replied"));

  std::string error_message;

  int id = -1;
  EXPECT_TRUE(GetIntFromJson(json, "id", &id, &error_message)) << error_message;
  EXPECT_EQ(kCompileTaskId, id);

  std::string state;
  EXPECT_TRUE(GetStringFromJson(json, "state", &state, &error_message))
      << error_message;
  EXPECT_EQ("INIT", state);

  ManuallyCleanUpCompileTask();
}

TEST_F(CompileTaskTest, DumpToJsonWithUnsuccessfulStart) {
  // Set the thread ID to something other than the curren thread to pass a
  // DCHECK().
  if (compile_task()->thread_id_ == GetCurrentThreadId()) {
    ++compile_task()->thread_id_;
  }
  // Running without having set proper compiler flags.
  compile_task()->Start();

  Json::Value json;
  compile_task()->DumpToJson(false, &json);
  EXPECT_FALSE(json.isMember("http"));
  EXPECT_TRUE(json.isMember("state"));

  std::string error_message;

  std::string state;
  EXPECT_TRUE(GetStringFromJson(json, "state", &state, &error_message))
      << error_message;
  EXPECT_EQ("FINISHED", state);

  // There should be no HTTP response code if there was no call to the server.
  int http = -1;
  EXPECT_FALSE(GetIntFromJson(json, "http", &http, &error_message))
      << error_message;
}

TEST_F(CompileTaskTest, DumpToJsonWithValidCallToServer) {
  // FakeExecServiceClient returns HTTP response code 200.
  compile_service()->SetExecServiceClient(
      absl::make_unique<FakeExecServiceClient>(200));
  // Force-set |state_| to enable ProcessCallExec() to run.
  compile_task()->state_ = CompileTask::FILE_REQ;
  compile_task()->ProcessCallExec();

  Json::Value json;
  compile_task()->DumpToJson(false, &json);
  EXPECT_TRUE(json.isMember("http"));
  EXPECT_TRUE(json.isMember("state"));

  std::string error_message;

  std::string state;
  EXPECT_TRUE(GetStringFromJson(json, "state", &state, &error_message))
      << error_message;
  EXPECT_EQ("FINISHED", state);

  int http = -1;
  EXPECT_TRUE(GetIntFromJson(json, "http", &http, &error_message))
      << error_message;
  EXPECT_EQ(200, http);
}

TEST_F(CompileTaskTest, DumpToJsonWithHTTPErrorCode) {
  // FakeExecServiceClient returns HTTP response code 403.
  compile_service()->SetExecServiceClient(
      absl::make_unique<FakeExecServiceClient>(403));
  // Force-set |state_| to enable ProcessCallExec() to run.
  compile_task()->state_ = CompileTask::FILE_REQ;
  compile_task()->ProcessCallExec();

  Json::Value json;
  compile_task()->DumpToJson(false, &json);

  std::string error_message;

  int http = -1;
  EXPECT_TRUE(GetIntFromJson(json, "http", &http, &error_message))
      << error_message;
  EXPECT_EQ(403, http);
}

TEST_F(CompileTaskTest, DumpToJsonWithDone) {
  compile_task()->state_ = CompileTask::FINISHED;
  compile_task()->rpc_ = nullptr;
  compile_task()->rpc_resp_ = nullptr;
  compile_task()->Done();

  Json::Value json;
  compile_task()->DumpToJson(false, &json);

  int replied = 0;
  std::string error_message;
  EXPECT_TRUE(GetIntFromJson(json, "replied", &replied, &error_message))
      << error_message;
  EXPECT_NE(0, replied);

  // No need to manually clean up in this test, since Done() is called.
}

TEST_F(CompileTaskTest, UpdateStatsFinished) {
  // Force-set |state_| to enable UpdateStats() to run.
  compile_task()->state_ = CompileTask::FINISHED;
  compile_task()->UpdateStats();

  ASSERT_TRUE(compile_task()->resp_);
  EXPECT_TRUE(compile_task()->resp_->compiler_proxy_goma_finished());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_cache_hit());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_local_finished());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_aborted());

  ManuallyCleanUpCompileTask();
}

TEST_F(CompileTaskTest, UpdateStatsFinishedCacheHit) {
  // Force-set |state_| to enable UpdateStats() to run.
  compile_task()->state_ = CompileTask::FINISHED;
  compile_task()->mutable_stats()->set_cache_hit(true);
  compile_task()->UpdateStats();

  ASSERT_TRUE(compile_task()->resp_);
  EXPECT_TRUE(compile_task()->resp_->compiler_proxy_goma_finished());
  EXPECT_TRUE(compile_task()->resp_->compiler_proxy_goma_cache_hit());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_local_finished());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_aborted());

  ManuallyCleanUpCompileTask();
}

TEST_F(CompileTaskTest, UpdateStatsLocalFinished) {
  // Force-set |state_| to enable UpdateStats() to run.
  compile_task()->state_ = CompileTask::LOCAL_FINISHED;
  compile_task()->UpdateStats();

  ASSERT_TRUE(compile_task()->resp_);
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_finished());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_cache_hit());
  EXPECT_TRUE(compile_task()->resp_->compiler_proxy_local_finished());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_aborted());

  ManuallyCleanUpCompileTask();
}

TEST_F(CompileTaskTest, UpdateStatsAborted) {
  // Force-set |state_| to enable UpdateStats() to run.
  compile_task()->abort_ = true;
  compile_task()->UpdateStats();

  ASSERT_TRUE(compile_task()->resp_);
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_finished());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_goma_cache_hit());
  EXPECT_FALSE(compile_task()->resp_->compiler_proxy_local_finished());
  EXPECT_TRUE(compile_task()->resp_->compiler_proxy_goma_aborted());

  ManuallyCleanUpCompileTask();
}

}  // namespace devtools_goma
