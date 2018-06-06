// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// goma_fetch is a tool to fetch from goma API endpoints.

#include <string.h>

#include <iostream>
#include <memory>
#include <sstream>

#include "absl/strings/string_view.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "env_flags.h"
#include "glog/logging.h"
#include "goma_init.h"
#include "http.h"
#include "http_init.h"
#include "ioutil.h"
#include "oauth2.h"
#include "platform_thread.h"
#include "socket_factory.h"
#include "worker_thread_manager.h"

#include "goma_flags.cc"

using std::string;
using devtools_goma::HttpClient;
using devtools_goma::PlatformThread;
using devtools_goma::WorkerThreadManager;
using devtools_goma::WorkerThreadRunner;

namespace {

// Fetcher fetches data by using HttpClient.
class Fetcher {
 public:
  // Takes ownership of HttpClient.
  explicit Fetcher(std::unique_ptr<HttpClient> client)
      : client_(std::move(client)) {
  }
  ~Fetcher() {
  }

  void Run() {
    client_->InitHttpRequest(&req_, "GET", "");
    req_.AddHeader("Connection", "close");

    int backoff_ms = client_->options().min_retry_backoff_ms;

    std::string err_messages;
    for (int i = 0; i < FLAGS_FETCH_RETRY; ++i) {
      err_messages += status_.err_message + " ";
      status_ = HttpClient::Status();
      client_->Do(&req_, &resp_, &status_);
      if (!status_.err) {
        if (status_.http_return_code >= 400 && status_.http_return_code < 500) {
          break;
        }
        if (status_.http_return_code == 200) {
          break;
        }
      }
      if (i + 1 < FLAGS_FETCH_RETRY) {
        LOG(WARNING) << "fetch fail try=" << i
                     << " err=" << status_.err
                     << " http code:" << status_.http_return_code
                     << " " << status_.err_message;
        backoff_ms = HttpClient::BackoffMsec(client_->options(),
                                             backoff_ms, true);
        LOG(INFO) << "backoff " << backoff_ms << "msec";
        PlatformThread::Sleep(backoff_ms);
      }
    }
    status_.err_message = err_messages + status_.err_message;
    LOG(INFO) << "get done " << status_.DebugString();
    client_->WaitNoActive();
    client_.reset();
  }

  const HttpClient::Status& status() const {
    return status_;
  }

  const devtools_goma::HttpResponse& resp() const {
    return resp_;
  }

 private:
  std::unique_ptr<HttpClient> client_;
  devtools_goma::HttpRequest req_;
  devtools_goma::HttpResponse resp_;
  HttpClient::Status status_;

  DISALLOW_COPY_AND_ASSIGN(Fetcher);
};

}  // anonymous namespace

int main(int argc, char* argv[], const char* envp[]) {
  devtools_goma::Init(argc, argv, envp);
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " url" << std::endl;
    exit(1);
  }
  // Initialize rand.
  srand(static_cast<unsigned int>(time(nullptr)));
  devtools_goma::InitLogging(argv[0]);
#ifdef _WIN32
  WinsockHelper wsa;
#endif

  WorkerThreadManager wm;
  wm.Start(2);

  HttpClient::Options http_options;
  devtools_goma::InitHttpClientOptions(&http_options);
  // clear extra params, like "?win".
  // request paths should be passed via argv[1].
  http_options.extra_params = "";
  if (!http_options.InitFromURL(argv[1])) {
    LOG(FATAL) << "Failed to initialize HttpClient::Options from URL:"
               << argv[1];
  }
  LOG(INFO) << "fetch " << argv[1];

  std::unique_ptr<HttpClient> client(new HttpClient(
      HttpClient::NewSocketFactoryFromOptions(http_options),
      HttpClient::NewTLSEngineFactoryFromOptions(http_options),
      http_options, &wm));

  std::unique_ptr<Fetcher> fetcher(new Fetcher(std::move(client)));

  std::unique_ptr<WorkerThreadRunner> fetch(
      new WorkerThreadRunner(
          &wm, FROM_HERE,
          devtools_goma::NewCallback(
              fetcher.get(),
              &Fetcher::Run)));
  devtools_goma::FlushLogFiles();
  fetch->Wait();
  LOG(INFO) << "fetch done";
  devtools_goma::FlushLogFiles();
  fetch.reset();
  wm.Finish();
  devtools_goma::FlushLogFiles();
  const HttpClient::Status& status = fetcher->status();
  if (status.err) {
    LOG(ERROR) << "fetch " << argv[1]
               << " err=" << status.err
               << " " << status.err_message
               << " " << http_options.DebugString();
    return 1;
  }
  LOG(INFO) << status.DebugString();
  absl::string_view body = fetcher->resp().Body();
  if (status.http_return_code != 200) {
    LOG(ERROR) << "fetch " << argv[1]
               << " http code:" << status.http_return_code
               << " " << status.err_message;
    LOG(INFO) << body;
    return 1;
  }
  devtools_goma::WriteStdout(body);
  return 0;
}
