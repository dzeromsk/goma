// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "settings.h"

#include <string>

#include "callback.h"
#include "compiler_specific.h"
#include "env_flags.h"
#include "glog/logging.h"
#include "http.h"
#include "http_init.h"
#include "http_rpc.h"
#include "http_rpc_init.h"
#include "mypath.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/settings.pb.h"
MSVC_POP_WARNING()
#include "util.h"
#include "worker_thread_manager.h"

#define GOMA_DECLARE_FLAGS_ONLY
#include "goma_flags.cc"

namespace devtools_goma {

void SettingsGetCall(HttpRPC* http_rpc,
                     SettingsReq* req, SettingsResp* resp,
                     HttpRPC::Status* status) {
  http_rpc->Call("", req, resp, status);
}

void ApplySettings(const string& settings_server,
                   const string& expect_settings,
                   WorkerThreadManager* wm) {
  HttpClient::Options http_options;
  InitHttpClientOptions(&http_options);
  http_options.InitFromURL(settings_server);
  HttpClient client(
      HttpClient::NewSocketFactoryFromOptions(http_options),
      HttpClient::NewTLSEngineFactoryFromOptions(http_options),
      http_options, wm);

  HttpRPC::Options http_rpc_options;
  InitHttpRPCOptions(&http_rpc_options);
  HttpRPC http_rpc(&client, http_rpc_options);

  HttpRPC::Status status;
  SettingsReq req;
  req.set_hostname(ToShortNodename(GetNodename()));
  if (!FLAGS_USE_CASE.empty()) {
    req.set_use_case(FLAGS_USE_CASE);
  }
  SettingsResp resp;

  LOG(INFO) << "Settings get from " << settings_server
            << " req=" << req.DebugString();
  std::unique_ptr<WorkerThreadRunner> call(
      new WorkerThreadRunner(
          wm, FROM_HERE,
          NewCallback(SettingsGetCall, &http_rpc, &req, &resp, &status)));
  call.reset();

  if (status.err) {
    LOG(ERROR) << "Settings.Get error: " << status.DebugString();
    if (!expect_settings.empty()) {
      LOG(FATAL) << "expect settings:" << expect_settings
                 << " but failed to get settings";
    }
    return;
  }
  if (resp.has_settings()) {
    LOG(INFO) << "Settings name=" << resp.settings().name();
    if (!resp.settings().endpoint_url().empty()) {
      HttpClient::Options o;
      o.InitFromURL(resp.settings().endpoint_url());
      LOG(INFO) << "endpoint url=" << resp.settings().endpoint_url()
                << " STUBBY_PROXY_IP_ADDRESS=" << o.dest_host_name
                << " STUBBY_PROXY_PORT=" << o.dest_port
                << " USE_SSL=" << o.use_ssl
                << " URL_PATH_PREFIX=" << o.url_path_prefix;
      FLAGS_STUBBY_PROXY_IP_ADDRESS = o.dest_host_name;
      FLAGS_STUBBY_PROXY_PORT = o.dest_port;
      FLAGS_USE_SSL = o.use_ssl;
      FLAGS_URL_PATH_PREFIX = o.url_path_prefix;
    }

    if (!resp.settings().certificate().empty()) {
      LOG(INFO) << "certificate=" << resp.settings().certificate();
      FLAGS_SSL_EXTRA_CERT_DATA = resp.settings().certificate();
    }
    LOG(INFO) << "Settings updated";
    if (!expect_settings.empty()) {
      CHECK_EQ(resp.settings().name(), expect_settings)
          << ": unexpected settings";
    }
  } else {
    LOG(WARNING) << "no settings";
    if (!expect_settings.empty()) {
      LOG(FATAL) << "expect settings:" << expect_settings
                 << " but no settings";
    }
  }
}

}  // namespace devtools_goma
