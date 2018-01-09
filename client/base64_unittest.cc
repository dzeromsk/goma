// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base64.h"

#include <gtest/gtest.h>

namespace devtools_goma {

TEST(Base64Test, Base64UrlEncodeRFC4648TestVector) {
  // https://tools.ietf.org/html/rfc4648#page-12
  EXPECT_EQ("", Base64UrlEncode("", true));
  EXPECT_EQ("Zg==", Base64UrlEncode("f", true));
  EXPECT_EQ("Zm8=", Base64UrlEncode("fo", true));
  EXPECT_EQ("Zm9v", Base64UrlEncode("foo", true));
  EXPECT_EQ("Zm9vYg==", Base64UrlEncode("foob", true));
  EXPECT_EQ("Zm9vYmE=", Base64UrlEncode("fooba", true));
  EXPECT_EQ("Zm9vYmFy", Base64UrlEncode("foobar", true));
}

TEST(Base64Test, Base64UrlEncodeRFC4648TestVectorNoPadding) {
  EXPECT_EQ("", Base64UrlEncode("", false));
  EXPECT_EQ("Zg", Base64UrlEncode("f", false));
  EXPECT_EQ("Zm8", Base64UrlEncode("fo", false));
  EXPECT_EQ("Zm9v", Base64UrlEncode("foo", false));
  EXPECT_EQ("Zm9vYg", Base64UrlEncode("foob", false));
  EXPECT_EQ("Zm9vYmE", Base64UrlEncode("fooba", false));
  EXPECT_EQ("Zm9vYmFy", Base64UrlEncode("foobar", false));
}

TEST(Base64Test, Base64UrlEncodeForJsonWebToken) {
  EXPECT_EQ("eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9",
            Base64UrlEncode("{\"alg\":\"RS256\",\"typ\":\"JWT\"}", false));

  EXPECT_EQ("eyJpc3MiOiI3NjEzMjY3OTgwNjktcjVtbGpsbG4xcmQ0bHJiaG"
            "c3NWVmZ2lncDM2bTc4ajVAZGV2ZWxvcGVyLmdzZXJ2aWNlYWNj"
            "b3VudC5jb20iLCJzY29wZSI6Imh0dHBzOi8vd3d3Lmdvb2dsZW"
            "FwaXMuY29tL2F1dGgvcHJlZGljdGlvbiIsImF1ZCI6Imh0dHBz"
            "Oi8vYWNjb3VudHMuZ29vZ2xlLmNvbS9vL29hdXRoMi90b2tlbi"
            "IsImV4cCI6MTMyODU1NDM4NSwiaWF0IjoxMzI4NTUwNzg1fQ",
            Base64UrlEncode("{"
                            "\"iss\":\"761326798069-"
                            "r5mljlln1rd4lrbhg75efgigp36m78j5"
                            "@developer.gserviceaccount.com\","
                            "\"scope\":\"https://www.googleapis.com/auth/"
                            "prediction\","
                            "\"aud\":\"https://accounts.google.com/o/oauth2/"
                            "token\","
                            "\"exp\":1328554385,\"iat\":1328550785}", false));
}

}  // namespace devtools_goma
