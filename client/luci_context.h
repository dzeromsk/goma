// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// https://github.com/luci/luci-py/blob/master/client/LUCI_CONTEXT.md

#ifndef DEVTOOLS_GOMA_CLIENT_LUCI_CONTEXT_H_
#define DEVTOOLS_GOMA_CLIENT_LUCI_CONTEXT_H_

#include <string>
#include <vector>

namespace devtools_goma {

struct LuciContextAuthAccount {
  // Logical identifier of the account (e.g "task" or "system").
  std::string id;
};

struct LuciContextAuth {
  // RPC port of LuciLocalAuthService
  int rpc_port;
  // secret used for OAuthTokenRequest.
  std::string secret;
  // list of accounts available through LUCI context.
  std::vector<LuciContextAuthAccount> accounts;
  // an account to use by default, see enabled().
  std::string default_account_id;

  LuciContextAuth() : rpc_port(-1) {}

  // Returns true if LUCI local auth should be used by default in this process.
  bool enabled() const {
    // There two flavors of the protocol:
    //  1. One doesn't use 'accounts' or 'default_account_id', and has local
    //     auth always enabled. This is deprecated.
    //  2. Another always uses 'accounts', and has local auth enabled only if
    //     'default_account_id' is set.
    return rpc_port > 0 && !secret.empty()
           && (accounts.empty() || !default_account_id.empty());
  }

  void clear() {
    rpc_port = -1;
    secret.clear();
    accounts.clear();
    default_account_id.clear();
  }
};

struct LuciContext {
  LuciContextAuth local_auth;
  // There may be more stuff here in the future.

  void clear() {
    local_auth.clear();
  }
};

struct LuciOAuthTokenRequest {
  std::vector<std::string> scopes;
  std::string secret;
  std::string account_id;

  std::string ToString() const;
};

struct LuciOAuthTokenResponse {
  // an error code (or 0 if success)
  int error_code;
  // optional error message
  std::string error_message;

  // the actual access token
  std::string access_token;
  // its expiration time, as unix timestamp
  int64_t expiry;

  LuciOAuthTokenResponse() : error_code(-1), expiry(-1) {}

  void clear() {
    error_code = -1;
    error_message.clear();
    access_token.clear();
    expiry = -1;
  }
};

// Parse LUCI_CONTEXT file contents.
// Returns false on invalid JSON.
// Or, return false if some required fields in LuciContextAuth are missing.
//
// Note that this function returns true even if local_auth is missing in
// JSON, please use valid() method before using what is in local_auth.
bool ParseLuciContext(
    const std::string& json_body, LuciContext* luci_context);

bool ParseLuciOAuthTokenResponse(
    const std::string& json_body, LuciOAuthTokenResponse* resp);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_LUCI_CONTEXT_H_
