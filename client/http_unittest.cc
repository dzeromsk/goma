// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "http.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "callback.h"
#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "lockhelper.h"
#include "mock_socket_factory.h"
#include "mypath.h"
#include "path.h"
#include "scoped_tmp_file.h"
#include "socket_factory.h"
#include "worker_thread.h"

namespace devtools_goma {

TEST(NetworkErrorStatusTest, Basic) {
  HttpClient::NetworkErrorStatus status(absl::Seconds(30));

  EXPECT_FALSE(status.NetworkErrorStartedTime().has_value());

  EXPECT_TRUE(status.OnNetworkErrorDetected(absl::FromTimeT(100)));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());
  EXPECT_EQ(100, absl::ToTimeT(*status.NetworkErrorStartedTime()));

  // Don't recover for 30 seconds.
  EXPECT_FALSE(status.OnNetworkRecovered(absl::FromTimeT(110)));
  EXPECT_EQ(100, absl::ToTimeT(*status.NetworkErrorStartedTime()));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());

  EXPECT_FALSE(status.OnNetworkRecovered(absl::FromTimeT(120)));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());
  EXPECT_EQ(100, absl::ToTimeT(*status.NetworkErrorStartedTime()));

  EXPECT_FALSE(status.OnNetworkRecovered(absl::FromTimeT(129)));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());
  EXPECT_EQ(100, absl::ToTimeT(*status.NetworkErrorStartedTime()));

  // Now recovered.
  EXPECT_TRUE(status.OnNetworkRecovered(absl::FromTimeT(131)));
  EXPECT_FALSE(status.NetworkErrorStartedTime().has_value());

  // Another network issue. (time=200)
  EXPECT_TRUE(status.OnNetworkErrorDetected(absl::FromTimeT(200)));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());
  EXPECT_EQ(200, absl::ToTimeT(*status.NetworkErrorStartedTime()));

  EXPECT_FALSE(status.OnNetworkRecovered(absl::FromTimeT(210)));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());
  EXPECT_EQ(200, absl::ToTimeT(*status.NetworkErrorStartedTime()));

  // Network error on time=220, so postpone to recover until time=250.
  EXPECT_FALSE(status.OnNetworkErrorDetected(absl::FromTimeT(220)));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());
  EXPECT_EQ(200, absl::ToTimeT(*status.NetworkErrorStartedTime()));

  EXPECT_FALSE(status.OnNetworkRecovered(absl::FromTimeT(249)));
  EXPECT_TRUE(status.NetworkErrorStartedTime().has_value());
  EXPECT_EQ(200, absl::ToTimeT(*status.NetworkErrorStartedTime()));

  // Now we consider the network is recovered.
  EXPECT_TRUE(status.OnNetworkRecovered(absl::FromTimeT(251)));
  EXPECT_FALSE(status.NetworkErrorStartedTime().has_value());
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
  EXPECT_TRUE(options.InitFromURL("https://oauth2.googleapis.com/token"));
  EXPECT_EQ("oauth2.googleapis.com", options.dest_host_name);
  EXPECT_EQ(443, options.dest_port);
  EXPECT_TRUE(options.use_ssl);
  EXPECT_EQ("/token", options.url_path_prefix);
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

TEST(HttpClientOptions, ProxyOptionsWitHTTP) {
  HttpClient::Options options;
  options.proxy_host_name = "proxy-example.com";
  options.proxy_port = 1234;
  EXPECT_TRUE(options.InitFromURL("http://example.com"));
  EXPECT_EQ("proxy-example.com", options.SocketHost());
  EXPECT_EQ(1234, options.SocketPort());
  EXPECT_EQ("example.com", options.Host());
  EXPECT_EQ("http://example.com:80/foo", options.RequestURL("foo"));
}

TEST(HttpClientOptions, ProxyOptionsWitHTTPS) {
  HttpClient::Options options;
  options.proxy_host_name = "proxy-example.com";
  options.proxy_port = 1234;
  EXPECT_TRUE(options.InitFromURL("https://example.com"));
  EXPECT_EQ("proxy-example.com", options.SocketHost());
  EXPECT_EQ(1234, options.SocketPort());
  EXPECT_EQ("example.com", options.Host());
  EXPECT_EQ("/foo", options.RequestURL("foo"));
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
    State state_ = State::INIT;
  };

  void SetUp() override {
    wm_ = absl::make_unique<WorkerThreadManager>();
    wm_->Start(1);
    pool_ = wm_->StartPool(1, "test");
    mock_server_ = absl::make_unique<MockSocketServer>(wm_.get());
    ASSERT_EQ(0, OpenSocketPairForTest(socks_));
  }

  void TearDown() override {
    mock_server_.reset();
    wm_->Finish();
    wm_.reset();
    pool_ = -1;
  }

  void ServerReceive(absl::string_view req, string* req_buf) {
    req_buf->resize(req.size());
    mock_server_->ServerRead(socks_[0], req_buf);
  }

  void ServerResponse(absl::string_view resp) {
    mock_server_->ServerWrite(socks_[0], string(resp));
  }

  void ServerClose() {
    mock_server_->ServerClose(socks_[0]);
  }
  void ServerWait(absl::Duration duration) {
    mock_server_->ServerWait(duration);
  }

  std::string ExpectedRequest(
      absl::string_view method, absl::string_view host) {
    return absl::StrCat(method, " / HTTP/1.1\r\n",
                        "Host: ", host, "\r\n",
                        "User-Agent: ", kUserAgentString, "\r\n",
                        "Content-Type: text/plain\r\n",
                        "Content-Length: 0\r\n",
                        "Connection: close\r\n",
                        "\r\n");
  }

  std::string ExpectedRequestWithBody(absl::string_view method,
                                      absl::string_view host,
                                      absl::string_view body) {
    return absl::StrCat(method, " / HTTP/1.1\r\n", "Host: ", host, "\r\n",
                        "User-Agent: ", kUserAgentString, "\r\n",
                        "Content-Type: text/plain\r\n",
                        "Content-Length: ", body.size(), "\r\n",
                        "Connection: close\r\n", "\r\n", body);
  }

  std::unique_ptr<HttpClient> NewHttpClient(absl::string_view host, int port) {
    std::unique_ptr<MockSocketFactory> socket_factory(
        absl::make_unique<MockSocketFactory>(socks_[1], &socket_status_));
    socket_factory->set_dest(absl::StrCat(host, ":", port));
    socket_factory->set_host_name(string(host));
    socket_factory->set_port(port);

    HttpClient::Options options;
    options.InitFromURL(absl::StrCat("http://", host, "/"));
    options.socket_read_timeout = absl::Milliseconds(200);
    return absl::make_unique<HttpClient>(
        std::move(socket_factory), nullptr, options, wm_.get());
  }

  void RunTest(TestContext* tc) {
    wm_->RunClosureInPool(
        FROM_HERE,
        pool_,
        NewCallback(
            this, &HttpClientTest::DoTest, tc),
        WorkerThread::PRIORITY_LOW);
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
        WorkerThread::PRIORITY_LOW);
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

  void ExpectSocketClosed(bool expect_closed) {
    if (expect_closed) {
      EXPECT_FALSE(socket_status_.is_owned());
      EXPECT_TRUE(socket_status_.is_closed());
      EXPECT_FALSE(socket_status_.is_released());
    } else {
      EXPECT_TRUE(socket_status_.is_owned());
      EXPECT_FALSE(socket_status_.is_closed());
      EXPECT_TRUE(socket_status_.is_released());
    }
  }

  std::unique_ptr<WorkerThreadManager> wm_;
  int pool_ = -1;
  std::unique_ptr<MockSocketServer> mock_server_;
  MockSocketFactory::SocketStatus socket_status_;
  int socks_[2];

  mutable Lock mu_;
  ConditionVariable cond_;
};

TEST_F(HttpClientTest, GetNoContentLengthConnectionClose) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n"
      "ok");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(OK, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("ok", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, GetNoContentLengthConnectionCloseSlowBody) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n");
  ServerResponse(
      "ok");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(OK, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("ok", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, GetNoContentLengthConnectionCloseEmptyBody) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(OK, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, GetEmptyBody) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(OK, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(false);
}

TEST_F(HttpClientTest, GetResponse) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 8\r\n\r\n"
      "response");

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(OK, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("response", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(false);
}

TEST_F(HttpClientTest, GetConnectionClose) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 8\r\n\r\n"
      "re");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_NE(OK, tc.status_.err);
    EXPECT_NE("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RECEIVING_RESPONSE, tc.status_.state);
    EXPECT_EQ(200, tc.status_.http_return_code);
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, GetTimedOut) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  tc.status_.timeouts.push_back(absl::Milliseconds(100));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }
  LOG(INFO) << "request sent";

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Content-Length: 8\r\n\r\n"
      "re");
  ServerWait(absl::Milliseconds(1500));
  ServerResponse(
      "sponse");

  LOG(INFO) << "waiting response";
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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(ERR_TIMEOUT, tc.status_.err);
    EXPECT_NE("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RECEIVING_RESPONSE, tc.status_.state);
    EXPECT_EQ(0, tc.status_.http_return_code);
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, Get204) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(OK, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(204, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, Get302) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 302 Found\r\n"
      "Content-Type: text/plain\r\n"
      "Location: http://example.com/dos_attack\r\n"
      "Connection: close\r\n"
      "\r\n"
      "redirect to http://example.com/dos_attack\r\n");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(FAIL, tc.status_.err);
    EXPECT_NE("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(302, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, Get401) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 401 Unauthorized\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n"
      "\r\n"
      "unauthorized request\r\n");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(FAIL, tc.status_.err);
    EXPECT_NE("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(401, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, Get502) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 502 Bad Gateway\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n"
      "\r\n"
      "server error\r\n");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(FAIL, tc.status_.err);
    EXPECT_NE("", tc.status_.err_message);
    EXPECT_EQ(HttpClient::Status::RESPONSE_RECEIVED, tc.status_.state);
    EXPECT_EQ(502, tc.status_.http_return_code);
    EXPECT_EQ("", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, GetFileDownload) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");

  ScopedTmpDir tmpdir("http_unittest_get_filedownload");
  EXPECT_TRUE(tmpdir.valid());
  string resp_file = file::JoinPath(tmpdir.dirname(), "resp");
  LOG(INFO) << "download to " << resp_file;
  HttpFileDownloadResponse resp(resp_file, 0644);
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n"
      "ok");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(200, tc.status_.http_return_code);

    string resp_data;
    EXPECT_TRUE(ReadFileToString(resp_file, &resp_data));
    EXPECT_EQ("ok", resp_data);
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, GetFileDownloadFail) {
  const string req_expected = ExpectedRequest("GET", "example.com");
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "GET", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");

  ScopedTmpDir tmpdir("http_unittest_get_filedownload");
  EXPECT_TRUE(tmpdir.valid());
  string resp_file = file::JoinPath(tmpdir.dirname(), "resp");
  LOG(INFO) << "download to " << resp_file;
  HttpFileDownloadResponse resp(resp_file, 0644);
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n"
      "no such file exists");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(FAIL, tc.status_.err);
    EXPECT_NE("", tc.status_.err_message);
    EXPECT_EQ(404, tc.status_.http_return_code);

    string resp_data;
    EXPECT_FALSE(ReadFileToString(resp_file, &resp_data));
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, Post) {
  constexpr absl::string_view kBody = "request body data";
  const string req_expected =
      ExpectedRequestWithBody("POST", "example.com", kBody);
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;
  HttpRequest req;
  client->InitHttpRequest(&req, "POST", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  req.SetBody(string(kBody));
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n"
      "ok");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("ok", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, PostUpload) {
  constexpr absl::string_view kBody = "request body data";
  const string req_expected =
      ExpectedRequestWithBody("POST", "example.com", kBody);
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;

  ScopedTmpDir tmpdir("http_unittest_post_upload");
  EXPECT_TRUE(tmpdir.valid());
  string req_file = file::JoinPath(tmpdir.dirname(), "req");
  ASSERT_TRUE(WriteStringToFile(kBody, req_file));
  LOG(INFO) << "upload from " << req_file;

  HttpFileUploadRequest req;
  client->InitHttpRequest(&req, "POST", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  req.SetBodyFile(req_file, kBody.size());
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_FALSE(tc.status_.finished);
  }

  ServerResponse(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/plain\r\n"
      "Connection: close\r\n\r\n"
      "ok");
  ServerClose();

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
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_EQ(0, tc.status_.err);
    EXPECT_EQ("", tc.status_.err_message);
    EXPECT_EQ(200, tc.status_.http_return_code);
    EXPECT_EQ("ok", resp.parsed_body());
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

TEST_F(HttpClientTest, PostUploadFailFileNotFound) {
  constexpr absl::string_view kBody = "request body data";
  const string req_expected =
      ExpectedRequestWithBody("POST", "example.com", kBody);
  string req_buf;
  ServerReceive(req_expected, &req_buf);

  std::unique_ptr<HttpClient> client(NewHttpClient("example.com", 80));

  bool done = false;

  ScopedTmpDir tmpdir("http_unittest_post_upload");
  EXPECT_TRUE(tmpdir.valid());
  string req_file = file::JoinPath(tmpdir.dirname(), "req");
  LOG(INFO) << "upload from " << req_file;

  HttpFileUploadRequest req;
  client->InitHttpRequest(&req, "POST", "");
  req.SetContentType("text/plain");
  req.AddHeader("Connection", "close");
  req.SetBodyFile(req_file, kBody.size());
  HttpResponse resp;
  TestContext tc(client.get(), &req, &resp, NewDoneCallback(&done));
  RunTest(&tc);
  {
    AutoLock lock(&mu_);
    while (tc.state_ != TestContext::State::CALL) {
      cond_.Wait(&mu_);
    }
    EXPECT_TRUE(tc.status_.connect_success);
    // might be finished because it failed to create request stream
    // or not yet.
  }

  Wait(&tc);
  {
    AutoLock lock(&mu_);
    while (!done) {
      cond_.Wait(&mu_);
    }
    while (tc.state_ != TestContext::State::DONE) {
      cond_.Wait(&mu_);
    }
    EXPECT_NE(req_expected, req_buf);
    EXPECT_TRUE(tc.status_.connect_success);
    EXPECT_TRUE(tc.status_.finished);
    EXPECT_NE(0, tc.status_.err);
    EXPECT_NE("", tc.status_.err_message);
  }
  client->WaitNoActive();
  ExpectSocketClosed(true);
}

bool HandleResponseBody(HttpClient::Response::Body* body,
                        absl::string_view response) {
    bool need_more = true;
    while (need_more) {
      char* ptr;
      int size;
      body->Next(&ptr, &size);
      if (response.size() <= size) {
        size = response.size();
      }
      memcpy(ptr, response.data(), size);
      response.remove_prefix(size);
      bool should_end = (size == 0 && response.empty());
      switch (body->Process(size)) {
        case HttpClient::Response::Body::State::Error:
          return false;
        case HttpClient::Response::Body::State::Ok:
          if (!response.empty()) {
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
    return true;
}

std::string ReadAllFromZeroCopyInputStream(
    google::protobuf::io::ZeroCopyInputStream* input) {
  std::string data;
  const void* buffer;
  int size;
  while (input->Next(&buffer, &size)) {
    data += std::string(static_cast<const char*>(buffer), size);
  }
  return data;
}

class HttpResponseBodyTest : public testing::Test {
 protected:
  bool ParsedBody(
      size_t content_length,
      bool is_chunked,
      EncodingType encoding_type,
      absl::string_view response,
      std::string* parsed_body) {
    std::unique_ptr<HttpResponse::Body> body =
        absl::make_unique<HttpResponse::Body>(
            content_length, is_chunked, encoding_type);
    if (body == nullptr) {
      return false;
    }

    parsed_body->clear();
    if (!HandleResponseBody(body.get(), response)) {
      return false;
    }
    std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> input =
        body->ParsedStream();
    if (input == nullptr) {
      return false;
    }
    *parsed_body = ReadAllFromZeroCopyInputStream(input.get());
    return true;
  }
};

TEST_F(HttpResponseBodyTest, NoContentLength) {
  static constexpr absl::string_view kBody = "response body";
  std::string parsed_body;
  EXPECT_TRUE(ParsedBody(string::npos, false, EncodingType::NO_ENCODING, kBody,
                         &parsed_body));
  EXPECT_EQ(kBody, parsed_body);
}

TEST_F(HttpResponseBodyTest, ContentLength) {
  static constexpr absl::string_view kBody = "response body";
  std::string parsed_body;
  EXPECT_FALSE(ParsedBody(kBody.size()-1, false, EncodingType::NO_ENCODING,
                          kBody, &parsed_body));

  EXPECT_TRUE(ParsedBody(kBody.size(), false, EncodingType::NO_ENCODING,
                         kBody, &parsed_body));
  EXPECT_EQ(kBody, parsed_body);
}

TEST_F(HttpResponseBodyTest, Chunked) {
  static constexpr absl::string_view kBody = "3\r\nabc\r\n"
                                             "0d\r\ndefghijklmnop\r\n"
                                             "a\r\nqrstuvwxyz\r\n"
                                             "0\r\n\r\n";
  std::string parsed_body;
  EXPECT_FALSE(ParsedBody(string::npos, true, EncodingType::NO_ENCODING,
                         kBody.substr(0, kBody.size()-1),
                         &parsed_body));

  EXPECT_TRUE(ParsedBody(string::npos, true, EncodingType::NO_ENCODING,
                         kBody, &parsed_body));

  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", parsed_body);
}

// TODO: test for ENCODING_DEFLATE, ENCODING_LZMA2

class HttpFileDownloadBodyTest : public testing::Test {
 protected:
  bool ParsedBody(
      size_t content_length,
      bool is_chunked,
      EncodingType encoding_type,
      absl::string_view response,
      std::string* parsed_body) {
    ScopedTmpDir tmpdir("http_http_file_download_body_test");
    if (!tmpdir.valid()) {
      LOG(ERROR) << "failed to create tmpdir";
      return false;
    }
    string tempfile = file::JoinPath(tmpdir.dirname(), "out");
    ScopedFd fd(ScopedFd::Create(tempfile, 0644));
    auto body = absl::make_unique<HttpFileDownloadResponse::Body>(
            std::move(fd), content_length, is_chunked, encoding_type);
    if (body == nullptr) {
      return false;
    }

    parsed_body->clear();
    if (!HandleResponseBody(body.get(), response)) {
      return false;
    }
    body.reset();

    if (!ReadFileToString(tempfile, parsed_body)) {
      LOG(ERROR) << "failed to read tempfile:" << tempfile;
      return false;
    }
    return true;
  }
};

TEST_F(HttpFileDownloadBodyTest, NoContentLength) {
  static constexpr absl::string_view kBody = "response body";
  std::string parsed_body;
  EXPECT_TRUE(ParsedBody(string::npos, false, EncodingType::NO_ENCODING, kBody,
                         &parsed_body));
  EXPECT_EQ(kBody, parsed_body);
}

TEST_F(HttpFileDownloadBodyTest, ContentLength) {
  static constexpr absl::string_view kBody = "response body";
  std::string parsed_body;
  EXPECT_FALSE(ParsedBody(kBody.size()-1, false, EncodingType::NO_ENCODING,
                          kBody, &parsed_body));

  EXPECT_TRUE(ParsedBody(kBody.size(), false, EncodingType::NO_ENCODING,
                         kBody, &parsed_body));
  EXPECT_EQ(kBody, parsed_body);
}

TEST_F(HttpFileDownloadBodyTest, BinaryFile) {
  std::string my_pathname = GetMyPathname();
  std::string binary_file;
  ASSERT_TRUE(ReadFileToString(my_pathname, &binary_file))
      << my_pathname;

  std::string parsed_body;
  EXPECT_TRUE(ParsedBody(binary_file.size(), false, EncodingType::NO_ENCODING,
                         binary_file, &parsed_body));
  EXPECT_EQ(binary_file, parsed_body);
}

TEST_F(HttpFileDownloadBodyTest, Chunked) {
  static constexpr absl::string_view kBody = "3\r\nabc\r\n"
                                             "0d\r\ndefghijklmnop\r\n"
                                             "a\r\nqrstuvwxyz\r\n"
                                             "0\r\n\r\n";
  std::string parsed_body;
  EXPECT_FALSE(ParsedBody(string::npos, true, EncodingType::NO_ENCODING,
                         kBody.substr(0, kBody.size()-1),
                         &parsed_body));

  EXPECT_TRUE(ParsedBody(string::npos, true, EncodingType::NO_ENCODING,
                         kBody, &parsed_body));

  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", parsed_body);
}

// TODO: test for ENCODING_DEFLATE, ENCODING_LZMA2

}  // namespace devtools_goma
