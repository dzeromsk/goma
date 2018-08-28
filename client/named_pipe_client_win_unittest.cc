// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "named_pipe_client_win.h"

#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "named_pipe_server_win.h"
#include "named_pipe_win.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

namespace {

constexpr absl::Duration kNamedPipeWaitTimeout = absl::Seconds(13);

}  // anonymous namespace

class NamedPipeClientTest : public ::testing::Test {
 public:
  class MockHandler : public NamedPipeServer::Handler {
   public:
    MockHandler() : wait_sec_(0) {}
    ~MockHandler() override {}
    void HandleIncoming(NamedPipeServer::Request* req) override {
      LOG(INFO) << "Handle incoming: msg=" << req->request_message();
      EXPECT_EQ(expect_request_, req->request_message());
      absl::SleepFor(absl::Seconds(wait_sec_));
      LOG(INFO) << "reply response: msg=" << reply_;
      req->SendReply(reply_);
    }

    void Transaction(const std::string& expect_req,
                     const std::string& reply) {
      expect_request_ = expect_req;
      reply_ = reply;
    }

    void SetWaitSec(int wait_sec) {
      wait_sec_ = wait_sec;
    }

   private:
    std::string expect_request_;
    std::string reply_;
    int wait_sec_;
  };
};

TEST(NamedPipeClientTest, Simple) {
  WorkerThreadManager wm;
  wm.Start(1);

  std::unique_ptr<NamedPipeClientTest::MockHandler> handler(
      new NamedPipeClientTest::MockHandler);
  static const char kReq[] = "POST /e HTTP/1.1\r\n";
  static const char kResp[] = "HTTP/1.1 200 OK\r\n";
  handler->Transaction(kReq, kResp);

  LOG(INFO) << "pipe server starts";
  NamedPipeServer server(&wm, handler.get());
  static const char kName[] = "named-pipe-client-win-unittest";
  server.Start(kName);

  LOG(INFO) << "pipe clients starts";
  NamedPipeFactory factory(kName, kNamedPipeWaitTimeout);
  ScopedNamedPipe pipe = factory.New();
  if (!pipe.valid()) {
    LOG_SYSRESULT(GetLastError());
  }
  ASSERT_TRUE(pipe.valid());

  LOG(INFO) << "send message " << kReq;
  ssize_t num_written =
      pipe.WriteWithTimeout(kReq, strlen(kReq), absl::Seconds(5));
  EXPECT_EQ(strlen(kReq), num_written);

  LOG(INFO) << "wait for response...";
  std::string buf;
  buf.resize(1024);
  ssize_t num_read =
      pipe.ReadWithTimeout(&buf[0], buf.size(), absl::Seconds(5));
  EXPECT_EQ(strlen(kResp), num_read);
  buf.resize(num_read);
  LOG(INFO) << "response=" << buf;
  EXPECT_EQ(kResp, buf);

  LOG(INFO) << "pipe server stopping...";
  server.Stop();

  wm.Finish();
}

TEST(NamedPipeClientTest, LargeResponse) {
  WorkerThreadManager wm;
  wm.Start(1);

  std::unique_ptr<NamedPipeClientTest::MockHandler> handler(
      new NamedPipeClientTest::MockHandler);
  static const char kReq[] = "POST /e HTTP/1.1\r\n";
  std::string resp = "HTTP/1.1 200 OK\r\n";
  const int kBufsize = 1024;
  // response is more than kBufsize
  // but less than kOutputBufSize in named_pipe_server_win.cc (64 * 1024).
  resp.resize(2 * 1024 + 512);
  handler->Transaction(kReq, resp);

  LOG(INFO) << "pipe server starts";
  NamedPipeServer server(&wm, handler.get());
  static const char kName[] = "named-pipe-client-win-unittest";
  server.Start(kName);

  LOG(INFO) << "pipe clients starts";
  NamedPipeFactory factory(kName, kNamedPipeWaitTimeout);
  ScopedNamedPipe pipe = factory.New();
  if (!pipe.valid()) {
    LOG_SYSRESULT(GetLastError());
  }
  ASSERT_TRUE(pipe.valid());

  LOG(INFO) << "send message " << kReq;
  ssize_t num_written =
      pipe.WriteWithTimeout(kReq, strlen(kReq), absl::Seconds(5));
  EXPECT_EQ(strlen(kReq), num_written);

  LOG(INFO) << "wait for response...";
  std::string received;
  for (;;) {
    LOG(INFO) << "received=" << received.size()
              << " try read=" << kBufsize;
    std::string buf;
    buf.resize(kBufsize);
    ssize_t num_read =
        pipe.ReadWithTimeout(&buf[0], buf.size(), absl::Seconds(5));
    if (num_read == 0) {
      break;
    }
    EXPECT_GT(num_read, 0)
        << "received=" << received.size()
        << " err=" << num_read;
    EXPECT_LE(num_read, kBufsize)
        << "received=" << received.size()
        << " read=" << num_read;
    buf.resize(num_read);
    received += buf;
    if (received.size() == resp.size()) {
      break;
    }
  }
  EXPECT_EQ(resp, received);

  LOG(INFO) << "pipe server stopping...";
  server.Stop();

  wm.Finish();
}

TEST(NamedPipeClientTest, LargeResponseThanOutputBuffer) {
  WorkerThreadManager wm;
  wm.Start(1);

  std::unique_ptr<NamedPipeClientTest::MockHandler> handler(
      new NamedPipeClientTest::MockHandler);
  static const char kReq[] = "POST /e HTTP/1.1\r\n";
  std::string resp = "HTTP/1.1 200 OK\r\n";
  // response is more than kOutputBufSize
  // in named_pipe_server_win.cc (128 * 1024).
  const int kRespBufsize = 130 * 1024;
  resp.resize(kRespBufsize);
  handler->Transaction(kReq, resp);

  LOG(INFO) << "pipe server starts";
  NamedPipeServer server(&wm, handler.get());
  static const char kName[] = "named-pipe-client-win-unittest";
  server.Start(kName);

  LOG(INFO) << "pipe clients starts";
  NamedPipeFactory factory(kName, kNamedPipeWaitTimeout);
  ScopedNamedPipe pipe = factory.New();
  if (!pipe.valid()) {
    LOG_SYSRESULT(GetLastError());
  }
  ASSERT_TRUE(pipe.valid());

  LOG(INFO) << "send message " << kReq;
  ssize_t num_written =
      pipe.WriteWithTimeout(kReq, strlen(kReq), absl::Seconds(5));
  EXPECT_EQ(strlen(kReq), num_written);

  LOG(INFO) << "wait for response...";
  std::string received;
  size_t bufsize = 1024;
  for (;;) {
    std::string buf;
    if (!received.empty()) {
      bufsize = kRespBufsize - received.size();
    }
    buf.resize(bufsize);
    LOG(INFO) << "received=" << received.size()
              << " try read=" << bufsize;
    ssize_t num_read =
        pipe.ReadWithTimeout(&buf[0], buf.size(), absl::Seconds(5));
    if (num_read == 0) {
      break;
    }
    EXPECT_GT(num_read, 0)
        << "received=" << received.size()
        << " err=" << num_read;
    EXPECT_LE(num_read, bufsize)
        << "received=" << received.size()
        << " read=" << num_read;
    buf.resize(num_read);
    received += buf;
    if (received.size() == resp.size()) {
      break;
    }
  }
  EXPECT_EQ(resp, received);

  LOG(INFO) << "pipe server stopping...";
  server.Stop();

  wm.Finish();
}

TEST(NamedPipeClientTest, Timeout) {
  WorkerThreadManager wm;
  wm.Start(1);

  std::unique_ptr<NamedPipeClientTest::MockHandler> handler(
      new NamedPipeClientTest::MockHandler);
  static const char kReq[] = "POST /e HTTP/1.1\r\n";
  static const char kResp[] = "HTTP/1.1 200 OK\r\n";
  handler->Transaction(kReq, kResp);
  handler->SetWaitSec(5);

  LOG(INFO) << "pipe server starts";
  NamedPipeServer server(&wm, handler.get());
  static const char kName[] = "named-pipe-client-win-unittest";
  server.Start(kName);

  LOG(INFO) << "pipe clients starts";
  NamedPipeFactory factory(kName, kNamedPipeWaitTimeout);
  ScopedNamedPipe pipe = factory.New();
  if (!pipe.valid()) {
    LOG_SYSRESULT(GetLastError());
  }
  ASSERT_TRUE(pipe.valid());

  LOG(INFO) << "send message " << kReq;
  ssize_t num_written =
      pipe.WriteWithTimeout(kReq, strlen(kReq), absl::Seconds(5));
  EXPECT_EQ(strlen(kReq), num_written);

  LOG(INFO) << "wait for response...";
  std::string received;
  const int kBufsize = 1024;
  for (;;) {
    std::string buf;
    buf.resize(kBufsize);
    LOG(INFO) << "received=" << received.size()
              << " try read=" << buf.size();
    ssize_t num_read =
        pipe.ReadWithTimeout(&buf[0], buf.size(), absl::Seconds(1));
    if (num_read == 0) {
      break;
    }
    if (num_read == ERR_TIMEOUT) {
      LOG(INFO) << "error timeout";
      absl::SleepFor(absl::Seconds(2));
      continue;
    }
    EXPECT_GT(num_read, 0)
        << "received=" << received.size()
        << " err=" << num_read;
    EXPECT_LE(num_read, buf.size())
        << "received=" << received.size()
        << " read=" << num_read;
    buf.resize(num_read);
    LOG(INFO) << "receive: " << buf;
    received += buf;
    if (received.size() == strlen(kResp)) {
      break;
    }
  }
  EXPECT_EQ(kResp, received);

  LOG(INFO) << "pipe server stopping...";
  server.Stop();

  wm.Finish();
}


}  // namespace devtools_goma
