// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oauth2.h"

#include <string>

#include <gtest/gtest.h>

#include "glog/logging.h"

using std::string;

namespace devtools_goma {

TEST(OAuth2Test, ParseOAuth2AccessToken) {
  static const char* kJsonResponse =
      "{\r\n"
      " \"access_token\": \"ya12.this_is_token\",\r\n"
      " \"token_type\": \"Bearer\",\r\n"
      " \"expires_in\": 3600\r\n"
      "}\r\n";

  string token_type;
  string access_token;
  int expires_in;
  EXPECT_TRUE(ParseOAuth2AccessToken(
      kJsonResponse, &token_type, &access_token, &expires_in));
  EXPECT_EQ("Bearer", token_type);
  EXPECT_EQ("ya12.this_is_token", access_token);
  EXPECT_EQ(3600, expires_in);
}

TEST(OAuth2Test, ParseOAuth2AccessTokenNoSpaces) {
  static const char* kJsonResponse =
      "{\r\n"
      " \"access_token\":\"1/fFBGRNJru1FQd44AzqT3Zg\",\r\n"
      " \"token_type\":\"Bearer\",\r\n"
      " \"expires_in\":3920\r\n"
      "}\r\n";

  string token_type;
  string access_token;
  int expires_in;
  EXPECT_TRUE(ParseOAuth2AccessToken(
      kJsonResponse, &token_type, &access_token, &expires_in));
  EXPECT_EQ("Bearer", token_type);
  EXPECT_EQ("1/fFBGRNJru1FQd44AzqT3Zg", access_token);
  EXPECT_EQ(3920, expires_in);
}

TEST(OAuth2Test, ParseOAuth2AccessTokenError) {
  static const char* kJsonResponse =
      "{\r\n"
      " \"error\" : \"authorization_pending\""
      "}\r\n";
  string token_type;
  string access_token;
  int expires_in;
  EXPECT_FALSE(ParseOAuth2AccessToken(
      kJsonResponse, &token_type, &access_token, &expires_in));
}

TEST(OAuth2Test, ParseOAuth2Config) {
  static const char* kConfigStr =
      "{\"auth_uri\": \"https://accounts.google.com/o/oauth2/auth\""
      ", \"redirect_uri\": \"http://localhost:57003\""
      ", \"client_id\": \"575346572923.apps.googleusercontent.com\""
      ", \"scope\": \"https://www.googleapis.com/auth/userinfo.email\""
      ", \"token_uri\": \"https://www.googleapis.com/oauth2/v3/token\""
      ", \"client_secret\": \"xxx_client_secret_xxx\""
      ", \"refresh_token\": \"xxx_refresh_token_xxx\""
      ", \"type\": \"authorized_user\"}";

  OAuth2Config config;
  EXPECT_TRUE(ParseOAuth2Config(kConfigStr, &config));
  EXPECT_TRUE(config.valid());
  EXPECT_EQ("https://accounts.google.com/o/oauth2/auth", config.auth_uri);
  EXPECT_EQ("https://www.googleapis.com/oauth2/v3/token", config.token_uri);
  EXPECT_EQ("https://www.googleapis.com/auth/userinfo.email", config.scope);
  EXPECT_EQ("575346572923.apps.googleusercontent.com", config.client_id);
  EXPECT_EQ("xxx_client_secret_xxx", config.client_secret);
  EXPECT_EQ("xxx_refresh_token_xxx", config.refresh_token);
  EXPECT_EQ("authorized_user", config.type);
}

TEST(OAuth2Test, ParseOAuth2ConfigWithoutType) {
  static const char* kConfigStr =
      "{\"auth_uri\": \"https://accounts.google.com/o/oauth2/auth\""
      ", \"redirect_uri\": \"http://localhost:57003\""
      ", \"client_id\": \"575346572923.apps.googleusercontent.com\""
      ", \"scope\": \"https://www.googleapis.com/auth/userinfo.email\""
      ", \"token_uri\": \"https://www.googleapis.com/oauth2/v3/token\""
      ", \"client_secret\": \"xxx_client_secret_xxx\""
      ", \"refresh_token\": \"xxx_refresh_token_xxx\"}";

  OAuth2Config config;
  EXPECT_TRUE(ParseOAuth2Config(kConfigStr, &config));
  EXPECT_TRUE(config.valid());
  EXPECT_EQ("https://accounts.google.com/o/oauth2/auth", config.auth_uri);
  EXPECT_EQ("https://www.googleapis.com/oauth2/v3/token", config.token_uri);
  EXPECT_EQ("https://www.googleapis.com/auth/userinfo.email", config.scope);
  EXPECT_EQ("575346572923.apps.googleusercontent.com", config.client_id);
  EXPECT_EQ("xxx_client_secret_xxx", config.client_secret);
  EXPECT_EQ("xxx_refresh_token_xxx", config.refresh_token);
  EXPECT_EQ("", config.type);
}

TEST(OAuth2Test, ParseOAuth2ConfigForChromeInfraAuth) {
  // https://chrome-infra-auth.appspot.com/auth/api/v1/server/oauth_config
  // with secret modification.
  static const char* kConfigStr =
      "{\"client_not_so_secret\": \"xxx_client_secret_xxx\""
      ", \"additional_client_ids\": "
      "[\"1037249634491-mvrb78t4pov1kcq626e4ipcemtfvv31k.apps."
      "googleusercontent.com\""
      ", \"174799409470-4nitjq4rqk8brkdl6nb8l2gagui5inuk.apps."
      "googleusercontent.com\""
      ", \"174799409470-8k3b89iov4racu9jrf7if3k4591voig3.apps."
      "googleusercontent.com\""
      ", \"174799409470-gbrk5dsauquu72522f8qpg4qo7oim2b5.apps."
      "googleusercontent.com\""
      ", \"446450136466-2hr92jrq8e6i4tnsa56b52vacp7t3936.apps."
      "googleusercontent.com\""
      ", \"5071639625-1lppvbtck1morgivc6sq4dul7klu27sd.apps."
      "googleusercontent.com\""
      ", \"553957813421-p7tl669udlpng6i0uqin89irf9uuuhqa.apps."
      "googleusercontent.com\""
      ", \"31977622648-utchjftf485h6q7fih17jdl7pusqabc4.apps."
      "googleusercontent.com\""
      ", \"32555940559.apps.googleusercontent.com\"]"
      ", \"client_id\": \"575346572923.apps.googleusercontent.com\""
      ", \"primary_url\": null"
      ", \"type\": \"authorized_user\"}";
  OAuth2Config config;
  EXPECT_TRUE(ParseOAuth2Config(kConfigStr, &config));
  EXPECT_EQ("https://accounts.google.com/o/oauth2/auth", config.auth_uri);
  EXPECT_EQ("https://www.googleapis.com/oauth2/v3/token", config.token_uri);
  EXPECT_EQ("https://www.googleapis.com/auth/userinfo.email", config.scope);
  EXPECT_EQ("575346572923.apps.googleusercontent.com", config.client_id);
  EXPECT_EQ("xxx_client_secret_xxx", config.client_secret);
  EXPECT_EQ("", config.refresh_token);
  EXPECT_EQ("authorized_user", config.type);
}

TEST(OAuth2Test, ParseOAuth2ConfigError) {
  static const char* kConfigStr =
     "{\"auth_uri\": \"https://accounts.google.com/o/oauth2/auth\""
     ", \"redirect_uri\": \"http://localhost:57003\""
     ", \"client_id\": \"575346572923.apps.googleusercontent.com\""
     ", \"scope\": \"https://www.googleapis.com/auth/userinfo.email\""
     ", \"token_uri\": \"https://www.googleapis.com/oauth2/v3/token\""
     ", \"client_secret\": \"\""
     ", \"refresh_token\": \"\"}";

  OAuth2Config config;
  EXPECT_FALSE(ParseOAuth2Config(kConfigStr, &config));
  EXPECT_FALSE(config.valid());
}

TEST(OAuth2Test, FormatOAuth2Config) {
  OAuth2Config config;
  config.auth_uri = "https://accounts.google.com/o/oauth2/auth";
  config.token_uri = "https://www.googleapis.com/oauth2/v3/token";
  config.scope = "https://www.googleapis.com/auth/userinfo.email";
  config.client_id = "575346572923.apps.googleusercontent.com";
  config.client_secret = "xxx_client_secret_xxx";
  config.refresh_token = "xxx_refresh_token_xxx";
  config.type = "authorized_user";

  EXPECT_TRUE(config.valid());
  string config_str = FormatOAuth2Config(config);
  LOG(INFO) << config_str;
  OAuth2Config got_config;
  EXPECT_TRUE(ParseOAuth2Config(config_str, &got_config));
  EXPECT_TRUE(got_config.valid());
  EXPECT_EQ(config.auth_uri, got_config.auth_uri);
  EXPECT_EQ(config.token_uri, got_config.token_uri);
  EXPECT_EQ(config.scope, got_config.scope);
  EXPECT_EQ(config.client_id, got_config.client_id);
  EXPECT_EQ(config.client_secret, got_config.client_secret);
  EXPECT_EQ(config.refresh_token, got_config.refresh_token);
  EXPECT_EQ(config.type, got_config.type);
}

TEST(OAuth2Test, ParseServiceAccountJson) {
  // This private key is the same as one in jwt_unittest.cc.

  static const char* kServiceAccountJson = "{\n"
      "  \"type\": \"service_account\",\n"
      "  \"project_id\": \"google.com:cxx-compiler-service\",\n"
      "  \"private_key_id\": \"c8c64bdffb032ad014993d4509521cbb4d64c388\",\n"
      "  \"private_key\": \"-----BEGIN PRIVATE KEY-----\\n"
      "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCJ2ljEsJpoZmrZ\\n"
      "AHTcs5HiFg9PkXUQJF4aK8jVacBl6C2U0YJGwnCCPYQHyju0++eZRWlAqds4Jn5O\\n"
      "8JclnLs5JFD6Qzlqosqwn4qu8QI7dy4PybjwxRZMQtWm5vY6gHmvID4WEvWjxjL2\\n"
      "mqVOdThYy2YV/3PsCyjf6Z2XYtAZZJoK94w4OpF30IF1wuEZHllh6VJ4wpRiqpT8\\n"
      "bHxSiMlH2CTaoKJowrgAoYENj5eSbnPP0dsSftdA3Ckeu5/A4OjhyrOCsjwZag6J\\n"
      "Ipw5oRRDm9iiRt7dHdtrjEkGsiaFZvqY4sW++8x8MGkPpO+Mc1IvJVjj7khOTHEH\\n"
      "mWORcjbTAgMBAAECggEAHmP0jeghIkLx60UefklYL++NEI2QsS5TUJG2hNX7hHvb\\n"
      "EKPfhJn5E71cDhuXbh7av/99ZLQNkCNsVRrVN4WGAOLwtzt6vPeGl8mUWVzokROF\\n"
      "JBXkn6/TapyRXWotflg0e1cwWM11OdXIBnWxW8qb0XeF2fOnKrKLIFHwXB98oRwn\\n"
      "G6jg3A3F+//PCvTNk+oTJUyNVIrF6MsLN2/a9CJwfQA4bDShnPlQj8ToXRf1mEqv\\n"
      "6i6NqgkXZX9q3jqU3/h66shUGR/ltc7aqsocHt1PJN0SCKPqxSJoGaZl/T7fCgVl\\n"
      "yvGoLrsyfX4WIW0BgICcfyyLwK5h48Gv1nq2kHiiAQKBgQDx6IYXbT4LhmHfJJ9d\\n"
      "3r6sxvBZ4h+0/HYVK/4rG4dvjSF/vVZvhXwKRbNybdRZoZiDp5QZBSN7TkPE8q97\\n"
      "8IQ91MggUqGSroVpU/PmGHIdUXMiU9qfq0F+KAXc5lNAunF4vqybWu16U4RFtpRq\\n"
      "joZKanb0Z0ChivQcI0YVDWNKcQKBgQCR4hbMTo3sHP0J4xKiisCBhkhN5wGo53bE\\n"
      "YIk1E+XE5u1Dp2gBPzhDilrG3PYphjwi0TvrAeWueJHdRJ2FJpe6BLsnJhJiKHkw\\n"
      "zVZHZ+Qn8+1WqnRobODzBXceqqHejDoeDfXBfTo94F6ttEu4EOIG6+1rVxOqaSD8\\n"
      "S52izO6PgwKBgDk4dS9pabm0KcZslT3RCG06CXRZZoKbDRto8pAjzN94FKpwkNeE\\n"
      "TZjob8/rZsVk0fyiUQeyDXiHRMR7W0MH21/8yvHKWemmWmxVrWWJ9sQ0lfVSvG30\\n"
      "RmOe9/QOjzbKYzjacV22HmJHCwyqaWTjHaTQlh6tpb4QbjmRpmwoZIohAoGAcos1\\n"
      "H2ImqVfxjsvOm/WaRZksOI7DjN2BMZwi35wp8zrm3RIa5a+/+7gsoqxoVB5kJWpo\\n"
      "Q5QPxbhBv5zameu9gn+oe4q3MH9a+OihcBuw13X9yui30i57ShXmfBu6UUWFdIe9\\n"
      "iRlMm70KWhWQxovrDUg9+OQ8OrelALRWp7eFMQUCgYEA4fz76VwkMrA8XzY326l5\\n"
      "36qU9oo4AVGN3Xtzh90C3cMYP3IpPTCdfxHvmyte2qC3uYb5EUtB15bX4UXR70bp\\n"
      "FypWqG6mgZ7Mdoh+PvInHDEuf8JdvwbhXlnhzHnfWi7+HjzWUUpS8Il0QuuIbE6q\\n"
      "pDh/d+sLfYP3TWpGOQ1yv6k=\\n"
      "-----END PRIVATE KEY-----\\n\",\n"
      "  \"client_email\": \"test@"
      "developer.gserviceaccount.com\", \n"
      "  \"client_id\": \"test.apps.googleusercontent.com\",\n"
      "  \"auth_uri\": \"https://accounts.google.com/o/oauth2/auth\",\n"
      "  \"token_uri\": \"https://accounts.google.com/o/oauth2/token\",\n"
      "  \"auth_provider_x509_cert_url\": "
      "\"https://www.googleapis.com/oauth2/v1/certs\",\n"
      "  \"client_x509_cert_url\": \"https://www.googleapis.com/"
      "robot/v1/metadata/x509/test%40developer.gserviceaccount.com\"\n"
      "}";
  ServiceAccountConfig saconfig;
  EXPECT_TRUE(ParseServiceAccountJson(kServiceAccountJson, &saconfig));
  EXPECT_EQ("google.com:cxx-compiler-service", saconfig.project_id);
  EXPECT_EQ("c8c64bdffb032ad014993d4509521cbb4d64c388",
            saconfig.private_key_id);

  EXPECT_EQ(
      "-----BEGIN PRIVATE KEY-----\n"
      "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCJ2ljEsJpoZmrZ\n"
      "AHTcs5HiFg9PkXUQJF4aK8jVacBl6C2U0YJGwnCCPYQHyju0++eZRWlAqds4Jn5O\n"
      "8JclnLs5JFD6Qzlqosqwn4qu8QI7dy4PybjwxRZMQtWm5vY6gHmvID4WEvWjxjL2\n"
      "mqVOdThYy2YV/3PsCyjf6Z2XYtAZZJoK94w4OpF30IF1wuEZHllh6VJ4wpRiqpT8\n"
      "bHxSiMlH2CTaoKJowrgAoYENj5eSbnPP0dsSftdA3Ckeu5/A4OjhyrOCsjwZag6J\n"
      "Ipw5oRRDm9iiRt7dHdtrjEkGsiaFZvqY4sW++8x8MGkPpO+Mc1IvJVjj7khOTHEH\n"
      "mWORcjbTAgMBAAECggEAHmP0jeghIkLx60UefklYL++NEI2QsS5TUJG2hNX7hHvb\n"
      "EKPfhJn5E71cDhuXbh7av/99ZLQNkCNsVRrVN4WGAOLwtzt6vPeGl8mUWVzokROF\n"
      "JBXkn6/TapyRXWotflg0e1cwWM11OdXIBnWxW8qb0XeF2fOnKrKLIFHwXB98oRwn\n"
      "G6jg3A3F+//PCvTNk+oTJUyNVIrF6MsLN2/a9CJwfQA4bDShnPlQj8ToXRf1mEqv\n"
      "6i6NqgkXZX9q3jqU3/h66shUGR/ltc7aqsocHt1PJN0SCKPqxSJoGaZl/T7fCgVl\n"
      "yvGoLrsyfX4WIW0BgICcfyyLwK5h48Gv1nq2kHiiAQKBgQDx6IYXbT4LhmHfJJ9d\n"
      "3r6sxvBZ4h+0/HYVK/4rG4dvjSF/vVZvhXwKRbNybdRZoZiDp5QZBSN7TkPE8q97\n"
      "8IQ91MggUqGSroVpU/PmGHIdUXMiU9qfq0F+KAXc5lNAunF4vqybWu16U4RFtpRq\n"
      "joZKanb0Z0ChivQcI0YVDWNKcQKBgQCR4hbMTo3sHP0J4xKiisCBhkhN5wGo53bE\n"
      "YIk1E+XE5u1Dp2gBPzhDilrG3PYphjwi0TvrAeWueJHdRJ2FJpe6BLsnJhJiKHkw\n"
      "zVZHZ+Qn8+1WqnRobODzBXceqqHejDoeDfXBfTo94F6ttEu4EOIG6+1rVxOqaSD8\n"
      "S52izO6PgwKBgDk4dS9pabm0KcZslT3RCG06CXRZZoKbDRto8pAjzN94FKpwkNeE\n"
      "TZjob8/rZsVk0fyiUQeyDXiHRMR7W0MH21/8yvHKWemmWmxVrWWJ9sQ0lfVSvG30\n"
      "RmOe9/QOjzbKYzjacV22HmJHCwyqaWTjHaTQlh6tpb4QbjmRpmwoZIohAoGAcos1\n"
      "H2ImqVfxjsvOm/WaRZksOI7DjN2BMZwi35wp8zrm3RIa5a+/+7gsoqxoVB5kJWpo\n"
      "Q5QPxbhBv5zameu9gn+oe4q3MH9a+OihcBuw13X9yui30i57ShXmfBu6UUWFdIe9\n"
      "iRlMm70KWhWQxovrDUg9+OQ8OrelALRWp7eFMQUCgYEA4fz76VwkMrA8XzY326l5\n"
      "36qU9oo4AVGN3Xtzh90C3cMYP3IpPTCdfxHvmyte2qC3uYb5EUtB15bX4UXR70bp\n"
      "FypWqG6mgZ7Mdoh+PvInHDEuf8JdvwbhXlnhzHnfWi7+HjzWUUpS8Il0QuuIbE6q\n"
      "pDh/d+sLfYP3TWpGOQ1yv6k=\n"
      "-----END PRIVATE KEY-----\n", saconfig.private_key);
  EXPECT_EQ("test@"
            "developer.gserviceaccount.com", saconfig.client_email);
  EXPECT_EQ("test.apps.googleusercontent.com", saconfig.client_id);
  EXPECT_EQ("https://accounts.google.com/o/oauth2/auth", saconfig.auth_uri);
  EXPECT_EQ("https://accounts.google.com/o/oauth2/token", saconfig.token_uri);

  EXPECT_EQ("https://www.googleapis.com/oauth2/v1/certs",
            saconfig.auth_provider_x509_cert_url);
  EXPECT_EQ("https://www.googleapis.com/robot/v1/metadata/x509/test"
            "%40developer.gserviceaccount.com",
            saconfig.client_x509_cert_url);
}

}  // namespace devtools_goma
