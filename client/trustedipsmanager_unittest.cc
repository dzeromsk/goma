// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "trustedipsmanager.h"

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>

using std::string;

namespace {

class TrustedipsmanagerTest : public ::testing::Test {
 protected:
  bool IsTrustedClient(const string& ip) {
    struct in_addr in;
    inet_aton(ip.c_str(), &in);
    return trusted_.IsTrustedClient(in);
  }

  devtools_goma::TrustedIpsManager trusted_;
};

TEST_F(TrustedipsmanagerTest, Basic) {
  EXPECT_EQ("TrustedClients[127.0.0.1/ffffffff]", trusted_.DebugString());
  EXPECT_TRUE(IsTrustedClient("127.0.0.1"));
  EXPECT_FALSE(IsTrustedClient("192.168.1.1"));
  EXPECT_FALSE(IsTrustedClient("192.168.1.2"));
  EXPECT_FALSE(IsTrustedClient("192.168.2.1"));
  EXPECT_FALSE(IsTrustedClient("10.0.0.1"));

  trusted_.AddAllow("192.168.1.1");
  EXPECT_EQ("TrustedClients[127.0.0.1/ffffffff,192.168.1.1/ffffffff]",
            trusted_.DebugString());
  EXPECT_TRUE(IsTrustedClient("192.168.1.1"));
  EXPECT_FALSE(IsTrustedClient("192.168.1.2"));

  trusted_.AddAllow("192.168.1.0/24");
  EXPECT_EQ("TrustedClients[127.0.0.1/ffffffff,192.168.1.1/ffffffff,"
            "192.168.1.0/ffffff00]", trusted_.DebugString());
  EXPECT_TRUE(IsTrustedClient("192.168.1.1"));
  EXPECT_TRUE(IsTrustedClient("192.168.1.2"));
  EXPECT_FALSE(IsTrustedClient("192.168.2.1"));

  trusted_.AddAllow("0.0.0.0/0");
  EXPECT_EQ("TrustedClients[127.0.0.1/ffffffff,192.168.1.1/ffffffff,"
            "192.168.1.0/ffffff00,0.0.0.0/0]", trusted_.DebugString());
  EXPECT_TRUE(IsTrustedClient("192.168.2.1"));
  EXPECT_TRUE(IsTrustedClient("10.0.0.1"));
}

}  // namespace
