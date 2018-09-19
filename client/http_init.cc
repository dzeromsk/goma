// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http_init.h"

#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "http.h"
#include "ioutil.h"
#include "oauth2.h"
#include "path.h"
#include "util.h"

#define GOMA_DECLARE_FLAGS_ONLY
#include "goma_flags.cc"

using std::string;

namespace devtools_goma {

namespace {

template<typename T>
static bool LoadConfig(const string& filename,
                       bool (*config_parser)(const string&, T*),
                       T* config) {
  string config_string;
  if (!ReadFileToString(filename.c_str(), &config_string)) {
    LOG(WARNING) << "Failed to read " << filename;
    return false;
  }
  if (!config_parser(config_string, config)) {
    LOG(WARNING) << "Failed to parse config in "
                 << filename
                 << " config_string=" << config_string;
    return false;
  }
  return true;
}

static string GetHomeDir() {
#ifndef _WIN32
  static const char *kHomeEnv = "HOME";
#else
  static const char *kHomeEnv = "USERPROFILE";
#endif
  return GetEnv(kHomeEnv);
}

}  // namespace

static void InitOAuth2(HttpClient::Options* http_options) {
    // allow if file doesn't exist, or invalid oauth config.
    // if not found, or invalid oauth config, start with logout state,
    // and user could login on status page (/api/loginz -
    // HandleLoginRequest below).
    if (!LoadConfig(FLAGS_OAUTH2_CONFIG_FILE,
                    ParseOAuth2Config,
                    &http_options->oauth2_config)) {
      DefaultOAuth2Config(&http_options->oauth2_config);
      LOG(INFO) << "Using default OAuth2 config.";
      SaveOAuth2Config(FLAGS_OAUTH2_CONFIG_FILE, http_options->oauth2_config);
    }
    CHECK(http_options->oauth2_config.enabled())
        << "Invalid OAuth2Config in "
        << FLAGS_OAUTH2_CONFIG_FILE;
}

void InitHttpClientOptions(HttpClient::Options* http_options) {
  http_options->proxy_host_name = FLAGS_PROXY_HOST;
  http_options->proxy_port = FLAGS_PROXY_PORT;

  // fields that would be updated by InitFromURL.
  http_options->dest_host_name = FLAGS_STUBBY_PROXY_IP_ADDRESS;
  http_options->dest_port = FLAGS_STUBBY_PROXY_PORT;
  http_options->use_ssl = FLAGS_USE_SSL;
  http_options->url_path_prefix = FLAGS_URL_PATH_PREFIX;

  http_options->extra_params = FLAGS_RPC_EXTRA_PARAMS;
  http_options->fail_fast = FLAGS_FAIL_FAST;

  http_options->reuse_connection = FLAGS_COMPILER_PROXY_REUSE_CONNECTION;

  // Attempt to load and interpret LUCI_CONTEXT. It may define options for an
  // ambient authentication in LUCI environment. We'll decide whether we will
  // use them few lines below. Note that LUCI_CONTEXT environment variable may
  // be defined even if ambient auth is not enabled.
  const string& luci_context_file = GetEnv("LUCI_CONTEXT");
  LuciContextAuth luci_context_auth;
  if (!luci_context_file.empty()) {
    LuciContext luci_context;
    CHECK(LoadConfig(luci_context_file,
                     ParseLuciContext,
                     &luci_context))
        << "LUCI_CONTEXT is set but cannot load it."
        << " filename=" << luci_context_file;
    luci_context_auth = luci_context.local_auth;
    LOG_IF(INFO, !luci_context_auth.enabled())
        << "Running under LUCI, but LUCI_CONTEXT auth is not enabled.";
  }

  // Preference order
  // - GOMA_HTTP_AUTHORIZATION_FILE
  //     - probably debug purpose only, overrides other settings.
  // - GOMA_OAUTH2_CONFIG_FILE
  //    - overrides service account setting for run buildbot locally to test.
  // - GOMA_SERVICE_ACCOUNT_JSON_FILE (maybe used in buildbots)
  // - GOMA_USE_GCE_SERVICE_ACCOUNT (maybe used in buildbots)
  // - LUCI_CONTEXT (ambient in luci environment, if it is enabled)
  // - default goma oauth2 config file
  //
  // Note: having OAuth2 config and LUCI_CONTEXT at once is valid.
  //       (crbug.com/684735#c14).
  if (!FLAGS_HTTP_AUTHORIZATION_FILE.empty()) {
    string auth_header;
    CHECK(ReadFileToString(FLAGS_HTTP_AUTHORIZATION_FILE.c_str(),
                           &auth_header))
        << FLAGS_HTTP_AUTHORIZATION_FILE
        << " : you need http Authorization header in "
        << FLAGS_HTTP_AUTHORIZATION_FILE
        << " or unset GOMA_HTTP_AUTHORIZATION_FILE";
    auth_header = string(absl::StripTrailingAsciiWhitespace(auth_header));
    http_options->authorization = auth_header;

    LOG_IF(WARNING, !FLAGS_OAUTH2_CONFIG_FILE.empty())
        << "GOMA_OAUTH2_CONFIG_FILE is set but ignored. "
        << FLAGS_OAUTH2_CONFIG_FILE;
    LOG_IF(WARNING, !FLAGS_SERVICE_ACCOUNT_JSON_FILE.empty())
        << "GOMA_SERVICE_ACCOUNT_JSON_FILE is set but ignored. "
        << FLAGS_SERVICE_ACCOUNT_JSON_FILE;
    LOG_IF(WARNING, !FLAGS_GCE_SERVICE_ACCOUNT.empty())
        << "GOMA_GCE_SERVICE_ACCOUNT is set but ignored. "
        << FLAGS_GCE_SERVICE_ACCOUNT;
    LOG_IF(WARNING, luci_context_auth.enabled())
        << "LUCI_CONTEXT auth is configured in the environment but ignored.";
  } else if (!FLAGS_OAUTH2_CONFIG_FILE.empty()) {
    InitOAuth2(http_options);

    LOG_IF(WARNING, !FLAGS_SERVICE_ACCOUNT_JSON_FILE.empty())
        << "GOMA_SERVICE_ACCOUNT_JSON_FILE is set but ignored. "
        << FLAGS_SERVICE_ACCOUNT_JSON_FILE;
    LOG_IF(WARNING, !FLAGS_GCE_SERVICE_ACCOUNT.empty())
        << "GOMA_GCE_SERVICE_ACCOUNT is set but ignored. "
        << FLAGS_GCE_SERVICE_ACCOUNT;
    LOG_IF(WARNING, luci_context_auth.enabled())
        << "LUCI_CONTEXT auth is configured in the environment but ignored.";

  } else if (!FLAGS_SERVICE_ACCOUNT_JSON_FILE.empty()) {
    // TODO: fallback if the file doesn't exit?
    http_options->service_account_json_filename =
        FLAGS_SERVICE_ACCOUNT_JSON_FILE;
    LOG_IF(WARNING, !FLAGS_GCE_SERVICE_ACCOUNT.empty())
        << "GOMA_GCE_SERVICE_ACCOUNT is set but ignored. "
        << FLAGS_GCE_SERVICE_ACCOUNT;
    LOG_IF(WARNING, luci_context_auth.enabled())
        << "LUCI_CONTEXT auth is configured in the environment but ignored.";

  } else if (!FLAGS_GCE_SERVICE_ACCOUNT.empty()) {
    http_options->gce_service_account = FLAGS_GCE_SERVICE_ACCOUNT;

    LOG_IF(WARNING, luci_context_auth.enabled())
        << "LUCI_CONTEXT auth is configured in the environment but ignored.";

  } else if (luci_context_auth.enabled()) {
    LOG(INFO) << "Using LUCI ambient authentication"
              << "  default_account_id="
              << luci_context_auth.default_account_id;
    http_options->luci_context_auth = luci_context_auth;

  } else {
    const string homedir = GetHomeDir();
    if (!homedir.empty()) {
      static constexpr absl::string_view kConfigFile =
          ".goma_client_oauth2_config";
      FLAGS_OAUTH2_CONFIG_FILE =
          file::JoinPath(homedir, kConfigFile);
      LOG(INFO) << "Use OAUTH2_CONFIG_FILE=" << FLAGS_OAUTH2_CONFIG_FILE;
      InitOAuth2(http_options);
    }
  }
  http_options->capture_response_header =
      FLAGS_HTTP_RPC_CAPTURE_RESPONSE_HEADER;
  http_options->ssl_extra_cert = FLAGS_SSL_EXTRA_CERT;
  http_options->ssl_extra_cert_data = FLAGS_SSL_EXTRA_CERT_DATA;
  if (FLAGS_SSL_CRL_MAX_VALID_DURATION >= 0) {
    http_options->ssl_crl_max_valid_duration =
        absl::Seconds(FLAGS_SSL_CRL_MAX_VALID_DURATION);
  }
  double http_socket_read_timeout_secs = 0;
  if (absl::SimpleAtod(FLAGS_HTTP_SOCKET_READ_TIMEOUT_SECS,
      &http_socket_read_timeout_secs)) {
    http_options->socket_read_timeout =
        absl::Seconds(http_socket_read_timeout_secs);
  } else {
    LOG(ERROR) << "Could not parse FLAGS_HTTP_SOCKET_READ_TIMEOUT_SECS: "
               << FLAGS_HTTP_SOCKET_READ_TIMEOUT_SECS;
  }
  http_options->min_retry_backoff =
      absl::Milliseconds(FLAGS_HTTP_RPC_MIN_RETRY_BACKOFF);
  http_options->max_retry_backoff =
      absl::Milliseconds(FLAGS_HTTP_RPC_MAX_RETRY_BACKOFF);
}

}  // namespace devtools_goma
