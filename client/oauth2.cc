// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oauth2.h"

#include <sstream>

#include "file_helper.h"
#include "glog/logging.h"
#include "ioutil.h"
#include "json/json.h"
#include "json_util.h"

using std::string;

namespace devtools_goma {

bool ParseOAuth2AccessToken(const string& json,
                            string* token_type,
                            string* access_token,
                            absl::Duration* expires_in) {
  static const char kAccessToken[] = "access_token";
  static const char kTokenType[] = "token_type";
  static const char kExpiresIn[] = "expires_in";

  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(json, root, false)) {
    LOG(WARNING) << "invalid json";
    return false;
  }

  string err;
  if (!GetStringFromJson(root, kAccessToken, access_token, &err)) {
    LOG(WARNING) << err;
    return false;
  }
  if (!GetStringFromJson(root, kTokenType, token_type, &err)) {
    LOG(WARNING) << err;
    return false;
  }
  int expires_in_sec = 0;
  if (!GetIntFromJson(root, kExpiresIn, &expires_in_sec, &err)) {
    LOG(WARNING) << err;
    return false;
  }

  if (expires_in_sec == 0) {
    return false;
  }
  *expires_in = absl::Seconds(expires_in_sec);

  return true;
}

static const char kAuthURI[] = "auth_uri";
static const char kTokenURI[] = "token_uri";
static const char kScope[] = "scope";
static const char kClientId[] = "client_id";
static const char kClientSecret[] = "client_secret";
// chrome-infra-auth.appspot oauth_config replies with client_not_so_secret.
static const char kClientNotSoSecret[] = "client_not_so_secret";
static const char kRefreshToken[] = "refresh_token";
static const char kType[] = "type";

// Google OAuth2 clients always have a secret, even if the client is an
// installed application/utility such as this.
// Please see following URL to understand why it is ok to do:
// https://chromium.googlesource.com/chromium/tools/depot_tools.git/+/master/auth.py
static const char kDefaultClientId[] =
    "687418631491-r6m1c3pr0lth5atp4ie07f03ae8omefc.apps.googleusercontent.com";
static const char kDefaultSecret[] = "R7e-JO3L5sKVczuR-dKQrijF";

void DefaultOAuth2Config(OAuth2Config* config) {
  config->auth_uri = kGoogleAuthURI;
  config->token_uri = kGoogleTokenURI;
  config->scope = kGomaAuthScope;
  config->client_id = kDefaultClientId;
  config->client_secret = kDefaultSecret;
  CHECK(config->enabled());
}

bool ParseOAuth2Config(const string& str, OAuth2Config* config) {
  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(str, root, false)) {
    LOG(WARNING) << "invalid json";
    return false;
  }

  string err;

  string auth_uri;
  if (!GetNonEmptyStringFromJson(root, kAuthURI, &auth_uri, &err)) {
    LOG(WARNING) << err;
    auth_uri = kGoogleAuthURI;
  }

  string token_uri;
  if (!GetNonEmptyStringFromJson(root, kTokenURI, &token_uri, &err)) {
    LOG(WARNING) << err;
    token_uri = kGoogleTokenURI;
  }

  string scope;
  if (!GetNonEmptyStringFromJson(root, kScope, &scope, &err)) {
    LOG(WARNING) << err;
    scope = kGomaAuthScope;
  }

  string client_id;
  if (!GetNonEmptyStringFromJson(root, kClientId, &client_id, &err)) {
    LOG(WARNING) << err;
    return false;
  }

  string client_secret;
  if (!GetNonEmptyStringFromJson(root, kClientSecret, &client_secret, &err)) {
    if (!GetNonEmptyStringFromJson(root, kClientNotSoSecret, &client_secret,
                                   &err)) {
      LOG(WARNING) << err;
      return false;
    }
  }

  string type;
  if (!GetNonEmptyStringFromJson(root, kType, &type, &err)) {
    LOG(WARNING) << err;
  }

  string refresh_token;
  (void)GetStringFromJson(root, kRefreshToken, &refresh_token, &err);

  config->auth_uri = auth_uri;
  config->token_uri = token_uri;
  config->scope = scope;
  config->client_id = client_id;
  config->client_secret = client_secret;
  config->refresh_token = refresh_token;
  config->type = type;
  return true;
}

string FormatOAuth2Config(const OAuth2Config& config) {
  Json::Value root;
  root[kAuthURI] = config.auth_uri;
  root[kTokenURI] = config.token_uri;
  root[kScope] = config.scope;
  root[kClientId] = config.client_id;
  root[kClientSecret] = config.client_secret;
  root[kRefreshToken] = config.refresh_token;
  root[kType] = config.type;
  Json::FastWriter writer;
  return writer.write(root);
}

bool SaveOAuth2Config(const string& filename, const OAuth2Config& config) {
  string config_string = FormatOAuth2Config(config);
  if (!WriteStringToFile(config_string, filename.c_str())) {
    LOG(ERROR) << "Failed to write " << filename;
    return false;
  }
  return true;
}

bool ParseServiceAccountJson(const string& str, ServiceAccountConfig* config) {
  // chrome-infra's /creds/service_accounts doesn't have
  // project_id, auth_uri, token_uri, auth_provider_x509_cert_url,
  // client_x509_cert_url, different from service account json
  // downloaded from google cloud console.

  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(str, root, false)) {
    LOG(WARNING) << "invalid json";
    return false;
  }

  string err;

  string type_str;
  if (!GetStringFromJson(root, "type", &type_str, &err)) {
    LOG(WARNING) << err;
    return false;
  }
  if (type_str != "service_account") {
    LOG(WARNING) << "unexpected type: " << type_str;
    return false;
  }

  string private_key;
  if (!GetNonEmptyStringFromJson(root, "private_key", &private_key, &err)) {
    LOG(WARNING) << err;
    return false;
  }

  string client_email;
  if (!GetNonEmptyStringFromJson(root, "client_email", &client_email, &err)) {
    LOG(WARNING) << err;
    return false;
  }

  string project_id;
  string private_key_id;
  string client_id;
  string auth_uri;
  string token_uri;
  string auth_provider_x509_cert_url;
  string client_x509_cert_url;
  (void)GetStringFromJson(root, "project_id", &project_id, &err);
  (void)GetStringFromJson(root, "private_key_id", &private_key_id, &err);
  (void)GetStringFromJson(root, "client_id", &client_id, &err);
  (void)GetStringFromJson(root, "auth_uri", &auth_uri, &err);
  (void)GetStringFromJson(root, "token_uri", &token_uri, &err);
  (void)GetStringFromJson(root, "auth_provider_x509_cert_url",
                  &auth_provider_x509_cert_url, &err);
  (void)GetStringFromJson(root, "client_x509_cert_url", &client_x509_cert_url,
                          &err);

  config->project_id = project_id;
  config->private_key_id = private_key_id;
  config->private_key = private_key;
  config->client_email = client_email;
  config->client_id = client_id;
  config->auth_uri = auth_uri;
  config->token_uri = token_uri;
  config->auth_provider_x509_cert_url = auth_provider_x509_cert_url;
  config->client_x509_cert_url = client_x509_cert_url;
  return true;
}

}  // namespace devtools_goma
