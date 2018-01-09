// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "luci_context.h"

#include <gtest/gtest.h>

#include "json/json.h"

namespace devtools_goma {

TEST(LuciContextTest, ParseLuciContextAuthSuccess) {
  static const char kLuciContext[] =
      "{\"local_auth\":{\"rpc_port\":54140,"
      "\"secret\":\"this_is_secret_string\","
      "\"accounts\":[{\"id\":\"acc_a\"},{\"id\":\"acc_b\"}],"
      "\"default_account_id\":\"acc_a\"}}";

  LuciContext luci_context;
  EXPECT_TRUE(ParseLuciContext(kLuciContext, &luci_context));
  EXPECT_TRUE(luci_context.local_auth.enabled());
  EXPECT_EQ(54140, luci_context.local_auth.rpc_port);
  EXPECT_EQ("this_is_secret_string", luci_context.local_auth.secret);
  EXPECT_EQ(2, luci_context.local_auth.accounts.size());
  EXPECT_EQ("acc_a", luci_context.local_auth.accounts[0].id);
  EXPECT_EQ("acc_b", luci_context.local_auth.accounts[1].id);
  EXPECT_EQ("acc_a", luci_context.local_auth.default_account_id);
}

TEST(LuciContextTest, ParseLuciContextAuthOldProtocol) {
  static const char kLuciContext[] =
      "{\"local_auth\":{\"rpc_port\":54140,"
      "\"secret\":\"this_is_secret_string\"}}";

  LuciContext luci_context;
  EXPECT_TRUE(ParseLuciContext(kLuciContext, &luci_context));
  EXPECT_TRUE(luci_context.local_auth.enabled());
}

TEST(LuciContextTest, ParseLuciContextAuthDisabled) {
  static const char kLuciContext[] =
      "{\"local_auth\":{\"rpc_port\":54140,"
      "\"secret\":\"this_is_secret_string\","
      "\"accounts\":[{\"id\":\"acc_a\"},{\"id\":\"acc_b\"}]}}";

  LuciContext luci_context;
  EXPECT_TRUE(ParseLuciContext(kLuciContext, &luci_context));
  EXPECT_FALSE(luci_context.local_auth.enabled());
}

TEST(LuciContextTest, ParseLuciContextAuthDisabledNull) {
  static const char kLuciContext[] =
      "{\"local_auth\":{\"rpc_port\":54140,"
      "\"secret\":\"this_is_secret_string\","
      "\"accounts\":[{\"id\":\"acc_a\"},{\"id\":\"acc_b\"}],"
      "\"default_account_id\":null}}";

  LuciContext luci_context;
  EXPECT_TRUE(ParseLuciContext(kLuciContext, &luci_context));
  EXPECT_FALSE(luci_context.local_auth.enabled());
}

TEST(LuciContextTest, ParseLuciContextAuthBadAccounts) {
  static const char kLuciContext[] =
      "{\"local_auth\":{\"rpc_port\":54140,"
      "\"secret\":\"this_is_secret_string\","
      "\"accounts\":[\"not an object\"],"
      "\"default_account_id\":\"acc_a\"}}";

  LuciContext luci_context;
  EXPECT_FALSE(ParseLuciContext(kLuciContext, &luci_context));
}

TEST(LuciContextTest, LuciOAuthTokenRequestToString) {
  LuciOAuthTokenRequest req;
  req.scopes.push_back("https://www.googleapis.com/auth/userinfo.email");
  req.scopes.push_back("https://www.googleapis.com/auth/plus.me");
  req.secret = "this_is_secret";
  req.account_id = "account_id";
  std::string request = req.ToString();

  Json::Reader reader;
  Json::Value root;
  EXPECT_TRUE(reader.parse(request, root, false));
  EXPECT_TRUE(root["scopes"].isArray());
  EXPECT_EQ(2U, root["scopes"].size());
  EXPECT_EQ("https://www.googleapis.com/auth/userinfo.email",
            root["scopes"][0].asString());
  EXPECT_EQ("https://www.googleapis.com/auth/plus.me",
            root["scopes"][1].asString());
  EXPECT_EQ("this_is_secret", root["secret"].asString());
  EXPECT_EQ("account_id", root["account_id"].asString());
}

TEST(LuciContextTest, ParseLuciOAuthTokenResponse) {
  static const char kResponse[] =
      "{\"access_token\":\"ya29.token\",\"expiry\":1487915944}";

  LuciOAuthTokenResponse resp;
  EXPECT_TRUE(ParseLuciOAuthTokenResponse(kResponse, &resp));
  EXPECT_EQ(0, resp.error_code);
  EXPECT_EQ("ya29.token", resp.access_token);
  EXPECT_EQ(1487915944, resp.expiry);
}

TEST(LuciContextTest, ParseLuciOAuthTokenResponseErrorCase) {
  static const char kResponse[] =
      "{\"error_code\": 123, \"error_message\": \"omg, error\"}";

  LuciOAuthTokenResponse resp;
  EXPECT_TRUE(ParseLuciOAuthTokenResponse(kResponse, &resp));
  EXPECT_EQ(123, resp.error_code);
  EXPECT_EQ("omg, error", resp.error_message);
  EXPECT_EQ("", resp.access_token);
}

}  // namespace devtools_goma
