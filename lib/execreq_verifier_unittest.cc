// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/execreq_verifier.h"

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
using google::protobuf::TextFormat;

namespace {

const char kValidExecReq[] = "command_spec {\n"
    "  name: \"gcc\"\n"
    "  version: \"4.4.3[Ubuntu 4.4.3-4ubuntu5]\"\n"
    "  target: \"x86_64-linux-gnu\"\n"
    "}\n"
    "arg: \"gcc\"\n"
    "arg: \"-c\"\n"
    "arg: \"hello.c\"\n"
    "cwd: \"/tmp\"\n";

}  // anonymous namespace

TEST(ExecreqVerifierTest, VerifyExecReq) {
  devtools_goma::ExecReq req;
  EXPECT_FALSE(devtools_goma::VerifyExecReq(req));

  ASSERT_TRUE(TextFormat::ParseFromString(kValidExecReq, &req));
  EXPECT_TRUE(devtools_goma::VerifyExecReq(req));

  req.mutable_command_spec()->clear_name();
  EXPECT_FALSE(devtools_goma::VerifyExecReq(req));

  ASSERT_TRUE(TextFormat::ParseFromString(kValidExecReq, &req));
  req.mutable_command_spec()->clear_version();
  EXPECT_FALSE(devtools_goma::VerifyExecReq(req));

  ASSERT_TRUE(TextFormat::ParseFromString(kValidExecReq, &req));
  req.mutable_command_spec()->clear_target();
  EXPECT_FALSE(devtools_goma::VerifyExecReq(req));

  ASSERT_TRUE(TextFormat::ParseFromString(kValidExecReq, &req));
  req.clear_command_spec();
  EXPECT_FALSE(devtools_goma::VerifyExecReq(req));

  ASSERT_TRUE(TextFormat::ParseFromString(kValidExecReq, &req));
  req.clear_arg();
  EXPECT_FALSE(devtools_goma::VerifyExecReq(req));

  ASSERT_TRUE(TextFormat::ParseFromString(kValidExecReq, &req));
  req.clear_cwd();
  EXPECT_FALSE(devtools_goma::VerifyExecReq(req));
}
