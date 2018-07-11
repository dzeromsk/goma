// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "execreq_normalizer.h"

#include "absl/strings/match.h"
#include "compiler_flag_type_specific.h"
#include "execreq_verifier.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
using google::protobuf::TextFormat;
using google::protobuf::util::MessageDifferencer;

namespace devtools_goma {

namespace {

const char kExecReqToNormalizeJavac[] =
    "command_spec {\n"
    "  name: \"javac\"\n"
    "  version: \"1.8.0_45-internal\"\n"
    "  target: \"java\"\n"
    "}\n"
    "arg: \"javac\"\n"
    "arg: \"-J-Xmx1024M\"\n"
    "arg: \"-Xmaxerrs\"\n"
    "arg: \"9999999\"\n"
    "arg: \"-encoding\"\n"
    "arg: \"UTF-8\"\n"
    "arg: \"-bootclasspath\"\n"
    "arg: \"-classpath\"\n"
    "arg: \"dummy.jar:dummy2.jar\"\n"
    "arg: \"-extdirs\"\n"
    "arg: \"-d\"\n"
    "arg: \"dest\"\n"
    "arg: \"-g\"\n"
    "arg: \"-encoding\"\n"
    "arg: \"UTF-8\"\n"
    "arg: \"-Xmaxwarns\"\n"
    "arg: \"9999999\"\n"
    "arg: \"-source\"\n"
    "arg: \"1.8\"\n"
    "arg: \"-target\"\n"
    "arg: \"1.8\"\n"
    "arg: \"hello.java\"\n"
    "cwd: \"/home/bob/src\"\n"
    "env: \"PWD=/home/bob/src\"\n"
    "Input {\n"
    "  filename: \"/home/bob/src/hello.java\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
    "559d507401ae81e9\"\n"
    "}\n";

void NormalizeExecReqForCacheKey(
    int id,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    ExecReq* req) {
  CompilerFlagTypeSpecific::FromArg(req->command_spec().name())
      .NewExecReqNormalizer()
      ->NormalizeForCacheKey(id, normalize_include_path, is_linking,
                             normalize_weak_relative_for_arg, debug_prefix_map,
                             req);
}

}  // namespace

// javac
TEST(JavacExecReqNormalizerTest, Normalize) {
  ExecReq req;

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalizeJavac, &req));
  ASSERT_TRUE(VerifyExecReq(req));

  // To confirm NormalizeExecReqForCacheKey omit them, let me add path that
  // won't exist in actual compile request.
  req.mutable_command_spec()->add_system_include_path("dummy");
  req.mutable_command_spec()->add_cxx_system_include_path("dummy");

  NormalizeExecReqForCacheKey(0, true, false, std::vector<string>(),
                              std::map<string, string>(), &req);

  EXPECT_EQ(0, req.command_spec().system_include_path_size());
  EXPECT_EQ(0, req.command_spec().cxx_system_include_path_size());

  EXPECT_EQ("", req.cwd());
  EXPECT_EQ(1, req.input_size());
  EXPECT_TRUE(req.input(0).has_filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// java
TEST(JavaExecReqNormalizerTest, Normalize) {
  static const char kExecReq[] = R"(
command_spec {
  name: "java"
  version: "1.8.0_45-internal"
  target: "java"
}
arg: "java"
arg: "-Xmx1024M"
arg: "-classpath"
arg: "/home/bob/java/classpath"
arg: "app.jar"
cwd: "/home/bob/java"
env: "PWD=/home/bob/java"
Input {
  filename: "/home/bob/java/classpath/app.jar"
  hash_key: "152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d559d507401ae81e9"
}
)";

  // Nothing will be normalized.
  static const char* const kExecReqExpected = kExecReq;

  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ExecReq req, req_expected;
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReq, &req));
  ASSERT_TRUE(VerifyExecReq(req));
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqExpected, &req_expected));
  ASSERT_TRUE(VerifyExecReq(req_expected));

  NormalizeExecReqForCacheKey(0, true, false, kTestOptions,
                              std::map<string, string>(), &req);

  MessageDifferencer differencer;
  string difference_reason;
  differencer.ReportDifferencesToString(&difference_reason);
  EXPECT_TRUE(differencer.Compare(req_expected, req)) << difference_reason;
}

}  // namespace devtools_goma
