// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "http.h"

#include <gtest/gtest.h>

namespace devtools_goma {

TEST(NetworkErrorStatus, BasicTest) {
  HttpClient::NetworkErrorStatus status(30);

  EXPECT_EQ(0, status.NetworkErrorStartedTime());

  EXPECT_TRUE(status.OnNetworkErrorDetected(100));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());

  // Don't recover for 30 seconds.
  EXPECT_FALSE(status.OnNetworkRecovered(110));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());
  EXPECT_FALSE(status.OnNetworkRecovered(120));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());
  EXPECT_FALSE(status.OnNetworkRecovered(129));
  EXPECT_EQ(100, status.NetworkErrorStartedTime());
  // Now recovered.
  EXPECT_TRUE(status.OnNetworkRecovered(131));
  EXPECT_EQ(0, status.NetworkErrorStartedTime());

  // Another network issue. (time=200)
  EXPECT_TRUE(status.OnNetworkErrorDetected(200));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());

  EXPECT_FALSE(status.OnNetworkRecovered(210));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());
  // Network error on time=220, so postpone to recover until time=250.
  EXPECT_FALSE(status.OnNetworkErrorDetected(220));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());

  EXPECT_FALSE(status.OnNetworkRecovered(249));
  EXPECT_EQ(200, status.NetworkErrorStartedTime());

  // Now we consider the network is recovered.
  EXPECT_TRUE(status.OnNetworkRecovered(251));
  EXPECT_EQ(0, status.NetworkErrorStartedTime());
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
  EXPECT_TRUE(options.InitFromURL(
      "https://www.googleapis.com/oauth2/v3/token"));
  EXPECT_EQ("www.googleapis.com", options.dest_host_name);
  EXPECT_EQ(443, options.dest_port);
  EXPECT_TRUE(options.use_ssl);
  EXPECT_EQ("/oauth2/v3/token", options.url_path_prefix);
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

}  // namespace devtools_goma
