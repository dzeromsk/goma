// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// goma_fetch is a tool to fetch from goma API endpoints.

#include <string.h>

#include <iostream>
#include <memory>
#include <sstream>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "env_flags.h"
#include "file_helper.h"
#include "gflags/gflags.h"
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
  Fetcher(std::unique_ptr<HttpClient> client,
          string method,
          HttpClient::Request* req,
          HttpClient::Response* resp)
      : client_(std::move(client)),
        method_(std::move(method)),
        req_(req),
        resp_(resp) {
  }
  ~Fetcher() {
  }

  void Run() {
    absl::Duration backoff = client_->options().min_retry_backoff;

    string err_messages;
    for (int i = 0; i < FLAGS_FETCH_RETRY; ++i) {
      client_->InitHttpRequest(req_, method_, "");
      resp_->Reset();
      err_messages += status_.err_message + " ";
      status_ = HttpClient::Status();
      client_->Do(req_, resp_, &status_);
      if (!status_.err) {
        LOG(INFO) << "http code:" << status_.http_return_code;
        break;
      }
      if (status_.http_return_code >= 400 && status_.http_return_code < 500) {
        LOG(WARNING) << "http code:" << status_.http_return_code;
        break;
      }
      if (i + 1 < FLAGS_FETCH_RETRY) {
        LOG(WARNING) << "fetch fail try=" << i
                     << " err=" << status_.err
                     << " http code:" << status_.http_return_code
                     << " " << status_.err_message;
        backoff = HttpClient::GetNextBackoff(client_->options(), backoff, true);
        LOG(INFO) << "backoff: " << backoff;
        absl::SleepFor(backoff);
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

 private:
  std::unique_ptr<HttpClient> client_;
  const string method_;
  HttpClient::Request* req_;
  HttpClient::Response* resp_;
  HttpClient::Status status_;

  DISALLOW_COPY_AND_ASSIGN(Fetcher);
};

DEFINE_bool(auth, true, "Enable Authentication.");
DEFINE_string(output, "", "Output filename.");
DEFINE_bool(head, false, "Do a request with HEAD method.");
DEFINE_bool(post, false, "Do a request with POST method.");
DEFINE_string(data, "", "Message body of POST request.");
DEFINE_string(data_file, "", "A file that has message body of POST request.");
DEFINE_string(content_type, "application/x-www-form-urlencoded",
              "content-type header used for POST request.");

}  // anonymous namespace

int main(int argc, char* argv[], const char* envp[]) {
  devtools_goma::Init(argc, argv, envp);
  const string usage = absl::StrCat(
      "An HTTP client for goma.\n",
      "Usage: ", argv[0], " [options...] <url>");
  gflags::SetUsageMessage(usage);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (argc < 2) {
    gflags::ShowUsageWithFlags(argv[0]);
    exit(1);
  }

  // Initialize rand.
  srand(static_cast<unsigned int>(time(nullptr)));
  devtools_goma::InitLogging(argv[0]);

  bool use_goma_auth = FLAGS_auth;
  absl::string_view method = "GET";
  absl::string_view output = "";
  absl::string_view url = argv[1];
  string body = "";
  if (FLAGS_head && FLAGS_post) {
    std::cerr << "You must not set both --head and --post at once."
              << std::endl;
    exit(1);
  } else if (FLAGS_head) {
    method = "HEAD";
  } else if (FLAGS_post) {
    method = "POST";
  }
  output = FLAGS_output;
  if (!FLAGS_data.empty() && !FLAGS_data_file.empty()) {
    std::cerr << "You must not set both --data and --data_file at once."
              << std::endl;
    exit(1);
  } else if (!FLAGS_data.empty()) {
    body = FLAGS_data;
  } else if (!FLAGS_data_file.empty()) {
    if (!devtools_goma::ReadFileToString(FLAGS_data_file, &body)) {
      std::cerr << "Failed to read a data file. "
                << FLAGS_data_file;
      exit(1);
    }
  }

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

  if (!use_goma_auth) {
    LOG(INFO) << "disable goma auth";
    http_options.authorization = "";
    http_options.oauth2_config.clear();
    http_options.gce_service_account = "";
    http_options.service_account_json_filename = "";
    http_options.luci_context_auth.clear();
  }

  if (!http_options.InitFromURL(url)) {
    LOG(FATAL) << "Failed to initialize HttpClient::Options from URL:"
               << url;
  }
  LOG(INFO) << "fetch " << method << " " << url;

  std::unique_ptr<HttpClient> client(
      absl::make_unique<HttpClient>(
      HttpClient::NewSocketFactoryFromOptions(http_options),
      HttpClient::NewTLSEngineFactoryFromOptions(http_options),
      http_options, &wm));

  HttpClient::Request* req = nullptr;
  auto httpreq(absl::make_unique<devtools_goma::HttpRequest>());
  httpreq->AddHeader("Connection", "close");
  if (!body.empty()) {
    httpreq->SetContentType(FLAGS_content_type);
    httpreq->SetBody(body);
  }
  req = httpreq.get();

  HttpClient::Response* resp = nullptr;
  std::unique_ptr<devtools_goma::HttpResponse> httpresp;
  std::unique_ptr<devtools_goma::HttpFileDownloadResponse> fileresp;
  if (output.empty()) {
    httpresp = absl::make_unique<devtools_goma::HttpResponse>();
    resp = httpresp.get();
  } else {
    fileresp = absl::make_unique<devtools_goma::HttpFileDownloadResponse>(
        string(output), 0644);
    resp = fileresp.get();
  }

  std::unique_ptr<Fetcher> fetcher(
      absl::make_unique<Fetcher>(std::move(client),
                                 string(method), req, resp));

  std::unique_ptr<WorkerThreadRunner> fetch(
      absl::make_unique<WorkerThreadRunner>(
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
    LOG(ERROR) << "fetch " << method << " " << url
               << " err=" << status.err
               << " " << status.err_message
               << " " << http_options.DebugString();
    return 1;
  }
  LOG(INFO) << status.DebugString();
  int exit_code = 0;
  if (status.http_return_code != 200 && status.http_return_code != 204) {
    LOG(ERROR) << "fetch " << method << " " << url
               << " http code:" << status.http_return_code
               << " " << status.err_message;
    exit_code = 1;
  }
  if (httpresp) {
    absl::string_view received_body = httpresp->parsed_body();
    if (exit_code) {
      LOG(INFO) << received_body;
      return exit_code;
    }
    devtools_goma::WriteStdout(received_body);
    return 0;
  }
  return exit_code;
}
