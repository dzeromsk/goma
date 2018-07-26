// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "http.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "callback.h"
#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "lockhelper.h"
#include "mock_socket_factory.h"
#include "socket_factory.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

TEST(NetworkErrorStatus, BasicTest) {
  HttpClient::NetworkErrorStatus status(30);

  EXPECT_EQ(0, status.NetworkErrorStartedTime());

  EXPECT_TRUE(status.OnNetworkErrorDetected(100));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());

  // Don't recover for 30 seconds.
  EXPECT_FALSE(status.OnNetworkRecovered(110));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());
  EXPECT_FALSE(status.OnNetworkRecovered(120));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());
  EXPECT_FALSE(status.OnNetworkRecovered(129));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());
  // Now recovered.
  EXPECT_TRUE(status.OnNetworkRecovered(131));
  EXPECT_EQ(0, status.NetworkErrorStartedTime());

  // Another network issue. (time=200)
  EXPECT_TRUE(status.OnNetworkErrorDetected(200));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());

  EXPECT_FALSE(status.OnNetworkRecovered(210));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());
  // Network error on time=220, so postpone to recover until time=250.
  EXPECT_FALSE(status.OnNetworkErrorDetected(220));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());

  EXPECT_FALSE(status.OnNetworkRecovered(249));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());

  // Now we consider the network is recovered.
  EXPECT_TRUE(status.OnNetworkRecovered(251));
  EXPECT_EQ(0, status.NetworkErrorStartedTime());
}

TEST(HttpClientOptions, InitFromURLChromeInfraAuth) {
  HttpClient::Options options;
  EXPECT_TRUE(options.InitFromURL(
      "https://chrome-infra-auth.appspot.com/auth/api/v1/server/oauth_config"));
  EXPECT_EQ("chrome-infra-auth.appspot.com", options.dest_host_name);
  EXPECT_EQ(443, options.dest_port);
  EXPECT_TRUE(options.use_ssl);
  EXPECT_EQ("/auth/api/v1/server/oauth_config", options.url_path_prefix);
}

TEST(HttpClientOptions, InitFromURLGCEMetadata) {
  HttpClient::Options options;
  EXPECT_TRUE(options.InitFromURL(
      "http://metadata/computeMetadata/v1/instance/service-accounts/"));
  EXPECT_EQ("metadata", options.dest_host_name);
  EXPECT_EQ(80, options.dest_port);
  EXPECT_FALSE(options.use_ssl);
  EXPECT_EQ("/computeMetadata/v1/instance/service-accounts/",
            options.url_path_prefix);
}

TEST(HttpClientOptions, InitFromURLGoogleOAuth2TokenURI) {
  HttpClient::Options options;
  EXPECT_TRUE(options.InitFromURL(
      "https://www.googleapis.com/oauth2/v3/token"));
  EXPECT_EQ("www.googleapis.com", options.dest_host_name);
  EXPECT_EQ(443, options.dest_port);
  EXPECT_TRUE(options.use_ssl);
  EXPECT_EQ("/oauth2/v3/token", options.url_path_prefix);
}

TEST(HttpClientOptions, InitFromURLWithExplicitPort) {
  HttpClient::Options options;
  EXPECT_TRUE(options.InitFromURL(
      "http://example.com:8080/foo/bar"));
  EXPECT_EQ("example.com", options.dest_host_name);
  EXPECT_EQ(8080, options.dest_port);
  EXPECT_FALSE(options.use_ssl);
  EXPECT_EQ("/foo/bar", options.url_path_prefix);
}

class HttpClientTest : public ::testing::Test {
 protected:
  class TestContext {
   public:
    enum class State { INIT, CALL, DONE };
    TestContext(HttpClient* client,
                HttpClient::Request* req,
                HttpClient::Response* resp,
                OneshotClosure* callback)
        : client_(client),
          req_(req),
          resp_(resp),
          callback_(callback) {
    }

    HttpClient* client_;
    HttpClient::Request* req_;
    HttpClient::Response* resp_;
    OneshotClosure* callback_;
    HttpClient::Status status_;
    int r_ = 0;
    State state_ = State::INIT;
  };

  void SetUp() override {
    wm_ = absl::make_unique<WorkerThreadManager>();
    wm_->Start(1);
    pool_ = wm_->StartPool(1, "test");
    mock_server_ = absl::make_unique<MockSocketServer>(wm_.get());
  }

  void TearDown() override {
    mock_server_.reset();
    wm_->Finish();
    wm_.reset();
    pool_ = -1;
  }

  void RunTest(TestContext* tc) {
    wm_->RunClosureInPool(
        FROM_HERE,
        pool_,
        NewCallback(
            this, &HttpClientTest::DoTest, tc),
        WorkerThreadManager::PRIORITY_LOW);
  }
  void DoTest(TestContext* tc) {
    tc->client_->DoAsync(tc->req_, tc->resp_,
                         &tc->status_,
                         tc->callback_);
    AutoLock lock(&mu_);
    tc->state_ = TestContext::State::CALL;
    cond_.Signal();
  }

  void Wait(TestContext* tc) {
    wm_->RunClosureInPool(
        FROM_HERE,
        pool_,
        NewCallback(
            this, &HttpClientTest::DoWait, tc),
        WorkerThreadManager::PRIORITY_LOW);
  }

  void DoWait(TestContext* tc) {
    tc->client_->Wait(&tc->status_);
    AutoLock lock(&mu_);
    tc->state_ = TestContext::State::DONE;
    cond_.Signal();
  }

  OneshotClosure* NewDoneCallback(bool* done) {
    {
      AutoLock lock(&mu_);
      *done = false;
    }
    return NewCallback(
        this, &HttpClientTest::DoneCallback, done);
  }

  void DoneCallback(bool* done) {
    AutoLock lock(&mu_);
    *done = true;
    cond_.Signal();
  }

  std::unique_ptr<WorkerThreadManager> wm_;
  int pool_ = -1;
  std::unique_ptr<MockSocketServer> mock_server_;
  mutable Lock mu_;
  ConditionVariable cond_;
};

TEST_F(HttpClientTest, GetNoContentLengthConnectionClose) {
  int socks[2];
  ASSERT_EQ(0, OpenSocketPairForTest(socks));
  std::string req_expected =
      absl::StrCat("GET / HTTP/1.1\r\n",
                   "Host: example.com\r\n",
                   "User-Agent: ", kUserAgentString, "\r\n",
                   "Content-Type: text/plain\r\n",
                   "Content-Length: 0\r\n",
                   "Connection: close\r\n",
                   "\r\n");
  string req_buf;
  req_buf.resize(req_expected.size());
  mock_server_->ServerRead(socks[0], &req_buf);

  MockSocketFactory::SocketStatus socket_status;
  std::unique_ptr<MockSocketFactory> socket_factory(
      absl::make_unique<MockSocketFactory>(socks[1], &socket_status));
  socket_factory->set_dest("example.com:80");
  socket_factory->set_host_name("example.com");
  socket_factory->set_port(80);

  HttpClient::Options options;
  options.InitFromURL("http://example.com/");
  HttpClient client(
      std::move(socket_factory), nullptr, options, wm_.get());

  bool done = false;
  HttpRequest req;
  client.InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(&client, &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  mock_server_->ServerWrite(
      socks[0],
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n"
      "ok");
  mock_server_->ServerClose(socks[0]);

  Wait(&tc);
  {
    AutoLock lock(&mu_);
    while (!done) {
      cond_.Wait(&mu_);
    }
    while (tc.state_ != TestContext::State::DONE) {
      cond_.Wait(&mu_);
    }
    EXPECT_EQ(req_expected, req_buf);
    EXPECT_EQ(0, tc.r_);
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("ok", resp.parsed_body());
  }
  client.WaitNoActive();
  EXPECT_FALSE(socket_status.is_owned());
  EXPECT_TRUE(socket_status.is_closed());
  EXPECT_FALSE(socket_status.is_released());
}

TEST_F(HttpClientTest, GetNoContentLengthConnectionCloseSlowBody) {
  int socks[2];
  ASSERT_EQ(0, OpenSocketPairForTest(socks));
  std::string req_expected =
      absl::StrCat("GET / HTTP/1.1\r\n",
                   "Host: example.com\r\n",
                   "User-Agent: ", kUserAgentString, "\r\n",
                   "Content-Type: text/plain\r\n",
                   "Content-Length: 0\r\n",
                   "Connection: close\r\n",
                   "\r\n");
  string req_buf;
  req_buf.resize(req_expected.size());
  mock_server_->ServerRead(socks[0], &req_buf);

  MockSocketFactory::SocketStatus socket_status;
  std::unique_ptr<MockSocketFactory> socket_factory(
      absl::make_unique<MockSocketFactory>(socks[1], &socket_status));
  socket_factory->set_dest("example.com:80");
  socket_factory->set_host_name("example.com");
  socket_factory->set_port(80);

  HttpClient::Options options;
  options.InitFromURL("http://example.com/");
  HttpClient client(
      std::move(socket_factory), nullptr, options, wm_.get());

  bool done = false;
  HttpRequest req;
  client.InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(&client, &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  mock_server_->ServerWrite(
      socks[0],
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n");
  mock_server_->ServerWrite(
      socks[0],
      "ok");
  mock_server_->ServerClose(socks[0]);

  Wait(&tc);
  {
    AutoLock lock(&mu_);
    while (!done) {
      cond_.Wait(&mu_);
    }
    while (tc.state_ != TestContext::State::DONE) {
      cond_.Wait(&mu_);
    }
    EXPECT_EQ(req_expected, req_buf);
    EXPECT_EQ(0, tc.r_);
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("ok", resp.parsed_body());
  }
  client.WaitNoActive();
  EXPECT_FALSE(socket_status.is_owned());
  EXPECT_TRUE(socket_status.is_closed());
  EXPECT_FALSE(socket_status.is_released());
}

TEST_F(HttpClientTest, GetNoContentLengthConnectionCloseEmptyBody) {
  int socks[2];
  ASSERT_EQ(0, OpenSocketPairForTest(socks));
  std::string req_expected =
      absl::StrCat("GET / HTTP/1.1\r\n",
                   "Host: example.com\r\n",
                   "User-Agent: ", kUserAgentString, "\r\n",
                   "Content-Type: text/plain\r\n",
                   "Content-Length: 0\r\n",
                   "Connection: close\r\n",
                   "\r\n");
  string req_buf;
  req_buf.resize(req_expected.size());
  mock_server_->ServerRead(socks[0], &req_buf);

  MockSocketFactory::SocketStatus socket_status;
  std::unique_ptr<MockSocketFactory> socket_factory(
      absl::make_unique<MockSocketFactory>(socks[1], &socket_status));
  socket_factory->set_dest("example.com:80");
  socket_factory->set_host_name("example.com");
  socket_factory->set_port(80);

  HttpClient::Options options;
  options.InitFromURL("http://example.com/");
  HttpClient client(
      std::move(socket_factory), nullptr, options, wm_.get());

  bool done = false;
  HttpRequest req;
  client.InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(&client, &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  mock_server_->ServerWrite(
      socks[0],
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n");
  mock_server_->ServerClose(socks[0]);

  Wait(&tc);
  {
    AutoLock lock(&mu_);
    while (!done) {
      cond_.Wait(&mu_);
    }
    while (tc.state_ != TestContext::State::DONE) {
      cond_.Wait(&mu_);
    }
    EXPECT_EQ(req_expected, req_buf);
    EXPECT_EQ(0, tc.r_);
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client.WaitNoActive();
  EXPECT_FALSE(socket_status.is_owned());
  EXPECT_TRUE(socket_status.is_closed());
  EXPECT_FALSE(socket_status.is_released());
}

TEST_F(HttpClientTest, GetEmptyBody) {
  int socks[2];
  ASSERT_EQ(0, OpenSocketPairForTest(socks));
  std::string req_expected =
      absl::StrCat("GET / HTTP/1.1\r\n",
                   "Host: example.com\r\n",
                   "User-Agent: ", kUserAgentString, "\r\n",
                   "Content-Type: text/plain\r\n",
                   "Content-Length: 0\r\n",
                   "Connection: close\r\n",
                   "\r\n");
  string req_buf;
  req_buf.resize(req_expected.size());
  mock_server_->ServerRead(socks[0], &req_buf);

  MockSocketFactory::SocketStatus socket_status;
  std::unique_ptr<MockSocketFactory> socket_factory(
      absl::make_unique<MockSocketFactory>(socks[1], &socket_status));
  socket_factory->set_dest("example.com:80");
  socket_factory->set_host_name("example.com");
  socket_factory->set_port(80);

  HttpClient::Options options;
  options.InitFromURL("http://example.com/");
  HttpClient client(
      std::move(socket_factory), nullptr, options, wm_.get());

  bool done = false;
  HttpRequest req;
  client.InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(&client, &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  mock_server_->ServerWrite(
      socks[0],
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 0\r\n\r\n");

  Wait(&tc);
  {
    AutoLock lock(&mu_);
    while (!done) {
      cond_.Wait(&mu_);
    }
    while (tc.state_ != TestContext::State::DONE) {
      cond_.Wait(&mu_);
    }
    EXPECT_EQ(req_expected, req_buf);
    EXPECT_EQ(0, tc.r_);
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client.WaitNoActive();
  EXPECT_TRUE(socket_status.is_owned());
  EXPECT_FALSE(socket_status.is_closed());
  EXPECT_TRUE(socket_status.is_released());
}

TEST_F(HttpClientTest, Get204) {
  int socks[2];
  ASSERT_EQ(0, OpenSocketPairForTest(socks));
  std::string req_expected =
      absl::StrCat("GET / HTTP/1.1\r\n",
                   "Host: example.com\r\n",
                   "User-Agent: ", kUserAgentString, "\r\n",
                   "Content-Type: text/plain\r\n",
                   "Content-Length: 0\r\n",
                   "Connection: close\r\n",
                   "\r\n");
  string req_buf;
  req_buf.resize(req_expected.size());
  mock_server_->ServerRead(socks[0], &req_buf);

  MockSocketFactory::SocketStatus socket_status;
  std::unique_ptr<MockSocketFactory> socket_factory(
      absl::make_unique<MockSocketFactory>(socks[1], &socket_status));
  socket_factory->set_dest("example.com:80");
  socket_factory->set_host_name("example.com");
  socket_factory->set_port(80);

  HttpClient::Options options;
  options.InitFromURL("http://example.com/");
  HttpClient client(
      std::move(socket_factory), nullptr, options, wm_.get());

  bool done = false;
  HttpRequest req;
  client.InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(&client, &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  mock_server_->ServerWrite(
      socks[0],
      "HTTP/1.1 204 No Content\r\n"
      "\r\n");

  Wait(&tc);
  {
    AutoLock lock(&mu_);
    while (!done) {
      cond_.Wait(&mu_);
    }
    while (tc.state_ != TestContext::State::DONE) {
      cond_.Wait(&mu_);
    }
    EXPECT_EQ(req_expected, req_buf);
    EXPECT_EQ(0, tc.r_);
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(204, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client.WaitNoActive();
  EXPECT_FALSE(socket_status.is_owned());
  EXPECT_TRUE(socket_status.is_closed());
  EXPECT_FALSE(socket_status.is_released());
}

class HttpResponseBodyTest : public testing::Test {
 protected:
  bool ParsedBody(
      size_t content_length,
      bool is_chunked,
      EncodingType encoding_type,
      absl::string_view content,
      std::string* parsed_body) {
    std::unique_ptr<HttpResponse::Body> body =
        absl::make_unique<HttpResponse::Body>(
            content_length, is_chunked, encoding_type);

    parsed_body->clear();
    bool need_more = true;
    while (need_more) {
      char* ptr;
      int size;
      body->Next(&ptr, &size);
      if (content.size() <= size) {
        size = content.size();
      }
      memcpy(ptr, content.data(), size);
      content.remove_prefix(size);
      bool should_end = (size == 0 && content.empty());
      switch (body->Process(size)) {
        case HttpClient::Response::Body::State::Error:
          return false;
        case HttpClient::Response::Body::State::Ok:
          if (!content.empty()) {
            return false;
          }
          need_more = false;
          break;
        case HttpClient::Response::Body::State::Incomplete:
          if (should_end) {
            return false;
          }
          continue;
      }
    }

    std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> input =
        body->ParsedStream();
    if (input == nullptr) {
      return false;
    }
    const void* buffer;
    int size;
    while (input->Next(&buffer, &size)) {
      *parsed_body += std::string(static_cast<const char*>(buffer), size);
    }
    return true;
  }
};

TEST_F(HttpResponseBodyTest, NoContentLength) {
  static constexpr absl::string_view kBody = "response body";
  std::string parsed_body;
  EXPECT_TRUE(ParsedBody(string::npos, false, NO_ENCODING, kBody,
                         &parsed_body));
  EXPECT_EQ(kBody, parsed_body);
}

TEST_F(HttpResponseBodyTest, ContentLength) {
  static constexpr absl::string_view kBody = "response body";
  std::string parsed_body;
  EXPECT_FALSE(ParsedBody(kBody.size()-1, false, NO_ENCODING, kBody,
                          &parsed_body));

  EXPECT_TRUE(ParsedBody(kBody.size(), false, NO_ENCODING, kBody,
                         &parsed_body));
  EXPECT_EQ(kBody, parsed_body);
}

TEST_F(HttpResponseBodyTest, Chunked) {
  static constexpr absl::string_view kBody = "3\r\nabc\r\n"
                                             "0d\r\ndefghijklmnop\r\n"
                                             "a\r\nqrstuvwxyz\r\n"
                                             "0\r\n\r\n";
  std::string parsed_body;
  EXPECT_FALSE(ParsedBody(string::npos, true, NO_ENCODING,
                         kBody.substr(0, kBody.size()-1),
                         &parsed_body));

  EXPECT_TRUE(ParsedBody(string::npos, true, NO_ENCODING, kBody,
                         &parsed_body));

  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", parsed_body);
}

// TODO: test for ENCODING_DEFLATE, ENCODING_LZMA2

}  // namespace devtools_goma
