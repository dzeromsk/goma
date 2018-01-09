// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "goma_ipc.h"

#include <string>
#include <sstream>

#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "ioutil.h"
#include "lockhelper.h"
#include "mock_socket_factory.h"
#ifdef _WIN32
#include "named_pipe_client_win.h"
#include "named_pipe_server_win.h"
#include "named_pipe_win.h"
#endif
#include "platform_thread.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "scoped_fd.h"
#include "socket_factory.h"
#include "worker_thread_manager.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

namespace devtools_goma {

#ifdef _WIN32
static const char kNamedPipeName[] = "goma-ipc-unittest";
#endif

class GomaIPCTest : public ::testing::Test {
 protected:
  class MockChanFactory : public GomaIPC::ChanFactory {
   public:
    explicit MockChanFactory(int sock)
        : factory_(new MockSocketFactory(sock)) {
    }
    ~MockChanFactory() override {}

    std::unique_ptr<IOChannel> New() override {
      ScopedSocket s(factory_->NewSocket());
      if (!s.valid()) {
        return nullptr;
      }
      return std::unique_ptr<IOChannel>(new ScopedSocket(std::move(s)));
    }

    std::string DestName() const override {
      return factory_->DestName();
    }

   private:
    std::unique_ptr<MockSocketFactory> factory_;
  };

#ifdef _WIN32
  class NamedPipeChanFactory : public GomaIPC::ChanFactory {
   public:
    NamedPipeChanFactory() : factory_(kNamedPipeName) {
    }
    ~NamedPipeChanFactory() override {}

    std::unique_ptr<IOChannel> New() override {
      ScopedNamedPipe pipe = factory_.New();
      if (!pipe.valid()) {
        return nullptr;
      }
      return std::unique_ptr<IOChannel>(new ScopedNamedPipe(std::move(pipe)));
    }

    string DestName() const override {
      return factory_.DestName();
    }

   private:
    NamedPipeFactory factory_;
  };

  class MockNamedPipeHandler : public NamedPipeServer::Handler {
   public:
    ~MockNamedPipeHandler() override {}
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
#endif

  void SetUp() override {
    wm_.reset(new WorkerThreadManager);
    wm_->Start(1);
    mock_server_.reset(new MockSocketServer(wm_.get()));
#ifdef _WIN32
    mock_handler_.reset(new MockNamedPipeHandler);
    named_pipe_server_.reset(
        new NamedPipeServer(wm_.get(), mock_handler_.get()));
    named_pipe_server_->Start(kNamedPipeName);
#endif
  }
  void TearDown() override {
#ifdef _WIN32
    named_pipe_server_->Stop();
    named_pipe_server_.reset();
    mock_handler_.reset();
#endif
    mock_server_.reset();
    wm_->Finish();
    wm_.reset();
  }
  std::unique_ptr<WorkerThreadManager> wm_;
  std::unique_ptr<MockSocketServer> mock_server_;
#ifdef _WIN32
  std::unique_ptr<MockNamedPipeHandler> mock_handler_;
  std::unique_ptr<NamedPipeServer> named_pipe_server_;
#endif
};

TEST_F(GomaIPCTest, ConnectFail) {
  std::unique_ptr<GomaIPC::ChanFactory> chan_factory(
      new MockChanFactory(-1));
  GomaIPC goma_ipc(std::move(chan_factory));
  GomaIPC::Status status;
  EmptyMessage req;
  HttpPortResponse resp;
  int r = goma_ipc.Call("/portz", &req, &resp, &status);
  EXPECT_EQ(FAIL, r);
  EXPECT_FALSE(status.connect_success);
  EXPECT_EQ(FAIL, status.err);
  EXPECT_EQ("Failed to connect to mock:80", status.error_message);
  EXPECT_EQ(0, status.http_return_code);
}

TEST_F(GomaIPCTest, CallPortz) {
  int socks[2];
  PCHECK(OpenSocketPairForTest(socks) == 0);
  EmptyMessage req;
  string serialized_req;
  req.SerializeToString(&serialized_req);
  std::ostringstream req_ss;
  req_ss << "POST /portz HTTP/1.1\r\n"
         << "Host: 0.0.0.0\r\n"
         << "User-Agent: " << kUserAgentString << "\r\n"
         << "Content-Type: binary/x-protocol-buffer\r\n"
         << "Content-Length: " << serialized_req.size() << "\r\n\r\n"
         << serialized_req;

  string req_expected = req_ss.str();
  string req_buf;
  req_buf.resize(req_expected.size());
  mock_server_->ServerRead(socks[0], &req_buf);
  HttpPortResponse resp;
  resp.set_port(8088);
  string serialized_resp;
  resp.SerializeToString(&serialized_resp);
  std::ostringstream resp_ss;
  resp_ss << "HTTP/1.1 200 OK\r\n"
          << "Content-Type: binary/x-protocol-buffer\r\n"
          << "Content-Length: " << serialized_resp.size() << "\r\n\r\n"
          << serialized_resp;
  mock_server_->ServerWrite(socks[0], resp_ss.str());
  resp.Clear();
  mock_server_->ServerClose(socks[0]);

  std::unique_ptr<GomaIPC::ChanFactory> chan_factory(
      new MockChanFactory(socks[1]));
  GomaIPC goma_ipc(std::move(chan_factory));
  GomaIPC::Status status;
  int r = goma_ipc.Call("/portz", &req, &resp, &status);
#ifdef _WIN32
  // it should fail on Windows, since peer is not named pipe.
  EXPECT_EQ(FAIL, r);
#else
  EXPECT_EQ(0, r);
  EXPECT_TRUE(status.connect_success);
  EXPECT_EQ(0, status.err);
  EXPECT_EQ("", status.error_message);
  EXPECT_EQ(200, status.http_return_code);
  EXPECT_EQ(req_expected, req_buf);
  EXPECT_TRUE(resp.IsInitialized());
  EXPECT_EQ(8088, resp.port());
#endif
}

#ifdef _WIN32
TEST_F(GomaIPCTest, CallPortzNamedPipewin) {
  EmptyMessage req;
  string serialized_req;
  req.SerializeToString(&serialized_req);
  std::ostringstream req_ss;
  req_ss << "POST /portz HTTP/1.1\r\n"
         << "Host: 0.0.0.0\r\n"
         << "User-Agent: " << kUserAgentString << "\r\n"
         << "Content-Type: binary/x-protocol-buffer\r\n"
         << "Content-Length: " << serialized_req.size() << "\r\n\r\n"
         << serialized_req;
  HttpPortResponse resp;
  resp.set_port(8088);
  string serialized_resp;
  resp.SerializeToString(&serialized_resp);
  std::ostringstream resp_ss;
  resp_ss << "HTTP/1.1 200 OK\r\n"
          << "Content-Type: binary/x-protocol-buffer\r\n"
          << "Content-Length: " << serialized_resp.size() << "\r\n\r\n"
          << serialized_resp;
  mock_handler_->Transaction(req_ss.str(), resp_ss.str());
  resp.Clear();

  std::unique_ptr<GomaIPC::ChanFactory> chan_factory(
      new NamedPipeChanFactory);
  GomaIPC goma_ipc(std::move(chan_factory));
  GomaIPC::Status status;
  int r = goma_ipc.Call("/portz", &req, &resp, &status);
  EXPECT_EQ(0, r);
  EXPECT_TRUE(status.connect_success);
  EXPECT_EQ(0, status.err);
  EXPECT_EQ("", status.error_message);
  EXPECT_EQ(200, status.http_return_code);
  EXPECT_TRUE(resp.IsInitialized());
  EXPECT_EQ(8088, resp.port());
}
#endif

}  // namespace devtools_goma
