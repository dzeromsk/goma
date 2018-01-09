// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "named_pipe_server_win.h"

#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "named_pipe_win.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

class NamedPipeServerTest : public ::testing::Test {
 public:
  class MockHandler : public NamedPipeServer::Handler {
   public:
    ~MockHandler() override {}
    void HandleIncoming(NamedPipeServer::Request* req) override {
      LOG(INFO) << "Handle incoming: msg=" << req->request_message();
      EXPECT_EQ(expect_request_, req->request_message());
      req->SendReply(reply_);
    }

    void Transaction(const std::string& expect_req,
                     const std::string& reply) {
      expect_request_ = expect_req;
      reply_ = reply;
    }

   private:
    std::string expect_request_;
    std::string reply_;
  };
};

TEST(NamedPipeServerTest, Simple) {
  WorkerThreadManager wm;
  wm.Start(1);

  std::unique_ptr<NamedPipeServerTest::MockHandler> handler(
      new NamedPipeServerTest::MockHandler);
  static const char kReq[] = "POST /e HTTP/1.1\r\n";
  static const char kResp[] = "HTTP/1.1 200 OK\r\n";
  handler->Transaction(kReq, kResp);

  LOG(INFO) << "pipe server starts";
  NamedPipeServer server(&wm, handler.get());
  server.Start("named-pipe-server-win-unittest");

  LOG(INFO) << "pipe clients starts";
  ScopedNamedPipe pipe(
      CreateFileA("\\\\.\\pipe\\named-pipe-server-win-unittest",
                  GENERIC_READ | GENERIC_WRITE,
                  0,
                  nullptr,
                  OPEN_EXISTING,
                  0,
                  nullptr));
  if (!pipe.valid()) {
    LOG_SYSRESULT(GetLastError());
  }
  ASSERT_TRUE(pipe.valid());

  LOG(INFO) << "pipe opened";

  LOG(INFO) << "send message " << kReq;
  DWORD num_bytes = 0;
  if (!WriteFile(pipe.get(), kReq, strlen(kReq), &num_bytes, nullptr)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to WriteFile to pipe";
    GTEST_FAIL();
  }
  EXPECT_EQ(strlen(kReq), num_bytes);

  LOG(INFO) << "wait for response...";
  num_bytes = 0;
  std::string buf;
  buf.resize(1024);
  if (!ReadFile(pipe.get(), &buf[0], buf.size(), &num_bytes, nullptr)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to ReadFile from pipe";
    GTEST_FAIL();
  }
  EXPECT_EQ(strlen(kResp), num_bytes);
  buf.resize(num_bytes);
  LOG(INFO) << "response=" << buf;
  EXPECT_EQ(kResp, buf);

  LOG(INFO) << "pipe server stopping...";
  server.Stop();

  wm.Finish();
}

}  // namespace devtools_goma
