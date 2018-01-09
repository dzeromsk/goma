// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "luci_context.h"

#include <string>

#include "glog/logging.h"
#include "json/json.h"
#include "json_util.h"

namespace devtools_goma {

namespace {

static bool ParseLocalAuth(const Json::Value& local_auth,
                           LuciContextAuth* luci_context_auth) {
  static const char kRpcPort[] = "rpc_port";
  static const char kSecret[] = "secret";
  static const char kAccounts[] = "accounts";
  static const char kDefaultAccountId[] = "default_account_id";
  static const char kId[] = "id";

  luci_context_auth->clear();

  if (!local_auth.isObject()) {
    LOG(WARNING) << "local_auth is not object";
    return false;
  }

  std::string err;
  if (!GetIntFromJson(local_auth, kRpcPort,
                      &luci_context_auth->rpc_port, &err)) {
    LOG(WARNING) << err;
    return false;
  }

  if (!GetStringFromJson(local_auth, kSecret,
                         &luci_context_auth->secret, &err)) {
    LOG(WARNING) << err;
    return false;
  }

  if (local_auth.isMember(kAccounts)) {
    const auto& accounts = local_auth[kAccounts];
    if (!accounts.isArray()) {
      LOG(WARNING) << "local_auth['accounts'] is not a list";
      return false;
    }
    for (const auto& account : accounts) {
      if (!account.isObject()) {
        LOG(WARNING) << "not an object in local_auth['accounts']";
        return false;
      }
      LuciContextAuthAccount luci_account;
      if (!GetStringFromJson(account, kId, &luci_account.id, &err)) {
        LOG(WARNING) << "error when reading account:" << err;
        return false;
      }
      luci_context_auth->accounts.push_back(luci_account);
    }
  }

  // Note: it can be missing or be null. In this case, LUCI authentication
  // should not be used by default. It is still valid LuciContextAuth object
  // though.
  if (local_auth.isMember(kDefaultAccountId) &&
      !local_auth[kDefaultAccountId].isNull()) {
    if (!GetStringFromJson(local_auth, kDefaultAccountId,
                           &luci_context_auth->default_account_id, &err)) {
      LOG(WARNING) << err;
      return false;
    }
  }

  return true;
}

}  // namespace

bool ParseLuciContext(
    const std::string& json_body, LuciContext* luci_context) {
  DCHECK(luci_context);
  static const char kLocalAuth[] = "local_auth";

  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(json_body, root, false)) {
    LOG(WARNING) << "invalid json";
    return false;
  }

  if (root.isMember(kLocalAuth)) {
    if (!ParseLocalAuth(root[kLocalAuth], &luci_context->local_auth)) {
      return false;
    }
  } else {
    LOG(INFO) << "missing " << kLocalAuth;
  }

  // TODO: implement swarming?
  // https://github.com/luci/luci-py/blob/master/client/LUCI_CONTEXT.md

  return true;
}

std::string LuciOAuthTokenRequest::ToString() const {
  static const char kScopes[] = "scopes";
  static const char kSecret[] = "secret";
  static const char kAccountId[] = "account_id";

  if (scopes.empty() || secret.empty()) {
    LOG(WARNING) << "trying to make string from invalid LuciOAuthTokenRequest";
    return std::string();
  }

  Json::Value root;
  for (const auto& scope : scopes) {
    root[kScopes].append(Json::Value(scope));
  }
  root[kSecret] = secret;

  // 'account_id' can be empty if using old protocol that doesn't allow
  // specifying accounts. See LuciContextAuth::enabled().
  if (!account_id.empty()) {
    root[kAccountId] = account_id;
  }

  Json::FastWriter writer;
  return writer.write(root);
}

bool ParseLuciOAuthTokenResponse(
    const std::string& json_body, LuciOAuthTokenResponse* resp) {
  DCHECK(resp);
  static const char kErrorCode[] = "error_code";
  static const char kErrorMessage[] = "error_message";
  static const char kAccessToken[] = "access_token";
  static const char kExpiry[] = "expiry";

  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(json_body, root, false)) {
    LOG(WARNING) << "invalid json";
    return false;
  }

  Json::Value default_error_code(0);
  const Json::Value& error_code = root.get(kErrorCode, default_error_code);
  if (!error_code.isInt()) {
    LOG(WARNING) << kErrorCode << " is not int";
    return false;
  }

  resp->error_code = error_code.asInt();
  if (resp->error_code != 0) {
    std::string err;
    if (!GetStringFromJson(root, kErrorMessage, &resp->error_message, &err)) {
      LOG(WARNING) << err
                   << " error_code=" << resp->error_code;
      return false;
    }
    return true;
  }

  std::string err;
  if (!GetStringFromJson(root, kAccessToken, &resp->access_token, &err)) {
    LOG(WARNING) << err;
    return false;
  }
  if (!GetInt64FromJson(root, kExpiry, &resp->expiry, &err)) {
    LOG(WARNING) << err;
    return false;
  }

  return true;
}

}  // namespace devtools_goma
