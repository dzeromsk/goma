// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "execreq_normalizer.h"

#include "compiler_flags.h"
#include "execreq_verifier.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "path.h"
using google::protobuf::TextFormat;
using google::protobuf::util::MessageDifferencer;

namespace devtools_goma {

namespace {

const char kExecReqToNormalizeWinAlice[] = R"(command_spec {
  name: "cl.exe"
  version: "19.00.24215.1"
  target: "x64"
  binary_hash: "7928c17d5185cf7dca794e9970e2463985315adf832f3fde7becfc71673d2fd3"
  local_compiler_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64\\cl.exe"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\um"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\shared"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\winrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\ucrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\atlmfc\\include"
}
arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
arg: "/nologo"
arg: "/showIncludes"
arg: "/FC"
arg: "@obj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj.rsp"
arg: "/c"
arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
cwd: "C:\\src\\chromium\\src\\out\\Alice"
Input {
  filename: "..\\..\\third_party\\boringssl\\src\\include\\openssl\\base.h"
  hash_key: "12812aef7084e6a0764657261ad92b9ef93a5e20aea26675324ad4b3c761a863"
}
Input {
  filename: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include\\algorithm"
  hash_key: "40908bd6f47550869ac26ac15b189d72f67187123066d8dfd2cf4435e0d53fc6"
}
expanded_arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
expanded_arg: "/nologo"
expanded_arg: "/showIncludes"
expanded_arg: "/FC"
expanded_arg: "-DI18N_PHONENUMBERS_NO_THREAD_SAFETY=1"
expanded_arg: "-Igen"
expanded_arg: "-I../../third_party/libaddressinput/src/cpp/include"
expanded_arg: "/D__DATE__="
expanded_arg: "/D__TIME__="
expanded_arg: "/D__TIMESTAMP__="
expanded_arg: "/Gy"
expanded_arg: "/FS"
expanded_arg: "/bigobj"
expanded_arg: "/d2FastFail"
expanded_arg: "/Zc:sizedDealloc-"
expanded_arg: "/W4"
expanded_arg: "/WX"
expanded_arg: "/utf-8"
expanded_arg: "/O1"
expanded_arg: "/Ob2"
expanded_arg: "/Oy-"
expanded_arg: "/d2Zi+"
expanded_arg: "/Zc:inline"
expanded_arg: "/Gw"
expanded_arg: "/Oi"
expanded_arg: "/MD"
expanded_arg: "/wd4267"
expanded_arg: "/TP"
expanded_arg: "/wd4577"
expanded_arg: "/GR-"
expanded_arg: "/c"
expanded_arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
expanded_arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
expanded_arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
)";

const char kExecReqToNormalizeWinBob[] = R"(command_spec {
  name: "cl.exe"
  version: "19.00.24215.1"
  target: "x64"
  binary_hash: "7928c17d5185cf7dca794e9970e2463985315adf832f3fde7becfc71673d2fd3"
  local_compiler_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64\\cl.exe"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\um"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\shared"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\winrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\win_sdk\\include\\10.0.15063.0\\ucrt"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include"
  cxx_system_include_path: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\atlmfc\\include"
}
arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
arg: "/nologo"
arg: "/showIncludes"
arg: "/FC"
arg: "@obj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj.rsp"
arg: "/c"
arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
cwd: "C:\\src\\chromium\\src\\out\\Bob"
Input {
  filename: "..\\..\\third_party\\boringssl\\src\\include\\openssl\\base.h"
  hash_key: "12812aef7084e6a0764657261ad92b9ef93a5e20aea26675324ad4b3c761a863"
}
Input {
  filename: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\win_sdk\\bin\\..\\..\\vc\\include\\algorithm"
  hash_key: "40908bd6f47550869ac26ac15b189d72f67187123066d8dfd2cf4435e0d53fc6"
}
expanded_arg: "c:\\src\\chromium\\src\\third_party\\depot_tools\\win_toolchain\\vs_files\\f53e4598951162bad6330f7a167486c7ae5db1e5\\vc\\bin\\amd64/cl.exe"
expanded_arg: "/nologo"
expanded_arg: "/showIncludes"
expanded_arg: "/FC"
expanded_arg: "-DI18N_PHONENUMBERS_NO_THREAD_SAFETY=1"
expanded_arg: "-Igen"
expanded_arg: "-I../../third_party/libaddressinput/src/cpp/include"
expanded_arg: "/D__DATE__="
expanded_arg: "/D__TIME__="
expanded_arg: "/D__TIMESTAMP__="
expanded_arg: "/Gy"
expanded_arg: "/FS"
expanded_arg: "/bigobj"
expanded_arg: "/d2FastFail"
expanded_arg: "/Zc:sizedDealloc-"
expanded_arg: "/W4"
expanded_arg: "/WX"
expanded_arg: "/utf-8"
expanded_arg: "/O1"
expanded_arg: "/Ob2"
expanded_arg: "/Oy-"
expanded_arg: "/d2Zi+"
expanded_arg: "/Zc:inline"
expanded_arg: "/Gw"
expanded_arg: "/Oi"
expanded_arg: "/MD"
expanded_arg: "/wd4267"
expanded_arg: "/TP"
expanded_arg: "/wd4577"
expanded_arg: "/GR-"
expanded_arg: "/c"
expanded_arg: "../../chrome/browser/ui/views/session_crashed_bubble_view.cc"
expanded_arg: "/Foobj/chrome/browser/ui/ui_3/session_crashed_bubble_view.obj"
expanded_arg: "/Fdobj/chrome/browser/ui/ui_3_cc.pdb"
)";

const char kExecReqToNormalizeWin[] =
    "command_spec {\n"
    "  name: \"cl.exe\"\n"
    "  version: \"15.00.30729.01\"\n"
    "  target: \"80x86\"\n"
    "  local_compiler_path: \"c:\\\\Program Files (x86)"
    "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\BIN\\\\cl.exe\"\n"
    "  system_include_path: \"c:\\\\Program Files (x86)"
    "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\INCLUDE\"\n"
    "  cxx_system_include_path: \"c:\\\\Program Files (x86)"
    "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\INCLUDE\"\n"
    "}\n"
    "arg: \"cl\"\n"
    "arg: \"/TP\"\n"
    "arg: \"/showIncludes\"\n"
    "arg: \"/Z7\"\n"
    "arg: \"/FoC:\\\\src\\\\goma\\\\client\\\\build\\\\Debug"
    "\\\\vc\\\\stdafx.obj\"\n"
    "arg: \"stdafx.cpp\"\n"
    "cwd: \"C:\\\\src\\\\goma\\\\client\\\\test\\\\vc\"\n"
    "Input {\n"
    "  filename: \"C:\\\\src\\\\goma\\\\client\\\\test\\\\vc\\\\stdafx.cpp\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
    "559d507401ae81e9\"\n"
    "}\n";

const char kExecReqToNormalizeWinClang[] =
    "command_spec {\n"
    "  name: \"clang-cl\"\n"
    "  version: \"clang version 7.0.0 (trunk 330570)\"\n"
    "  target: \"i686-windows-msvc\"\n"
    "  local_compiler_path: \"c:\\\\Program Files (x86)"
    "\\\\LLVM\\\\clang-cl.exe\"\n"
    "  system_include_path: \"c:\\\\Program Files (x86)"
    "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\INCLUDE\"\n"
    "  cxx_system_include_path: \"c:\\\\Program Files (x86)"
    "\\\\Microsoft Visual Studio 9.0\\\\VC\\\\INCLUDE\"\n"
    "}\n"
    "arg: \"clang-cl\"\n"
    "arg: \"/TP\"\n"
    "arg: \"-fprofile-instr-generate\"\n"
    "arg: \"-fcoverage-mapping\"\n"
    "arg: \"/FoC:\\\\src\\\\goma\\\\client\\\\build\\\\Debug"
    "\\\\vc\\\\stdafx.obj\"\n"
    "arg: \"stdafx.cpp\"\n"
    "cwd: \"C:\\\\src\\\\goma\\\\client\\\\test\\\\vc\"\n"
    "Input {\n"
    "  filename: \"C:\\\\src\\\\goma\\\\client\\\\test\\\\vc\\\\stdafx.cpp\"\n"
    "  hash_key: \"152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d"
    "559d507401ae81e9\"\n"
    "}\n";

}  // namespace

TEST(VCExecReqNormalizerTest, NormalizeExecReqForCacheKeyForClExe) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalizeWin, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(0, true, false, kTestOptions,
                                             std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  const string expected_include_path(
      "c:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\INCLUDE");
  EXPECT_EQ(expected_include_path, req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ(expected_include_path,
            req.command_spec().cxx_system_include_path(0));
  EXPECT_FALSE(req.cwd().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ("C:\\src\\goma\\client\\test\\vc\\stdafx.cpp",
            req.input(0).filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// cl.exe with different cwd and /showIncludes option
TEST(VCExecReqNormalizerTest, NormalizeExecReqForCacheKeyForClExeCWD) {
  devtools_goma::ExecReq alice_req, bob_req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(
      TextFormat::ParseFromString(kExecReqToNormalizeWinAlice, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalizeWinBob, &bob_req));

  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &bob_req);

  EXPECT_FALSE(MessageDifferencer::Equals(alice_req, bob_req));

  // check only |cwd| is different.
  ASSERT_TRUE(
      TextFormat::ParseFromString(kExecReqToNormalizeWinAlice, &alice_req));
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalizeWinBob, &bob_req));

  ASSERT_TRUE(devtools_goma::VerifyExecReq(alice_req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(bob_req));

  alice_req.clear_cwd();
  bob_req.clear_cwd();

  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &alice_req);
  devtools_goma::NormalizeExecReqForCacheKey(
      0, true, false, kTestOptions, std::map<string, string>(), &bob_req);

  MessageDifferencer differencer;
  string difference_reason;
  differencer.ReportDifferencesToString(&difference_reason);
  EXPECT_TRUE(differencer.Compare(alice_req, bob_req)) << difference_reason;
}

TEST(VCExecReqNormalizerTest, NormalizeExecReqForCacheKeyForClang) {
  devtools_goma::ExecReq req;
  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqToNormalizeWinClang, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  devtools_goma::NormalizeExecReqForCacheKey(0, true, false, kTestOptions,
                                             std::map<string, string>(), &req);
  EXPECT_EQ(1, req.command_spec().system_include_path_size());
  static const char kEexpectedIncludePath[] =
      "c:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\INCLUDE";
  EXPECT_EQ(kEexpectedIncludePath, req.command_spec().system_include_path(0));
  EXPECT_EQ(1, req.command_spec().cxx_system_include_path_size());
  EXPECT_EQ(kEexpectedIncludePath,
            req.command_spec().cxx_system_include_path(0));
  EXPECT_FALSE(req.cwd().empty());
  EXPECT_EQ(1, req.input_size());
  EXPECT_EQ("C:\\src\\goma\\client\\test\\vc\\stdafx.cpp",
            req.input(0).filename());
  EXPECT_TRUE(req.input(0).has_hash_key());
}

// When keep_args in exec req normalizer can be non kAsIs, arg can be
// normalized. But not yet.
TEST(VCExecReqNormalizerTest,
     NormalizeExecReqForCacheKeyForClang_NormalizeArgs) {
  static const char kExecReq[] = R"###(
command_spec {
  name: "clang-cl"
  version: "clang version 7.0.0 (trunk 330570)"
  target: "i686-windows-msvc"
  local_compiler_path: "C:\\Program Files (x86)\\LLVM\\clang-cl.exe"
  system_include_path: "C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\INCLUDE"
  system_include_path: "C:\\src\\goma\\client\\test\\vc\\include"
  cxx_system_include_path: "C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\INCLUDE"
}
arg: "clang-cl"
arg: "/TP"
arg: "-fprofile-instr-generate"
arg: "-fcoverage-mapping"
arg: "/FoC:\\src\\goma\\client\\build\\Debug\\vc\\stdafx.obj"
arg: "/IC:\\src\\goma\\client\\test\\vc\\include"
arg: "stdafx.cpp"
cwd: "C:\\src\\goma\\client\\test\\vc"
Input {
  filename: "C:\\src\\goma\\client\\test\\vc\\stdafx.cpp"
  hash_key: "152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d559d507401ae81e9"
}
)###";

  static const char kExecReqExpected[] = R"###(
command_spec {
  name: "clang-cl"
  version: "clang version 7.0.0 (trunk 330570)"
  target: "i686-windows-msvc"
  system_include_path: "C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\INCLUDE"
  system_include_path: "C:\\src\\goma\\client\\test\\vc\\include"
  cxx_system_include_path: "C:\\Program Files (x86)\\Microsoft Visual Studio 9.0\\VC\\INCLUDE"
}
arg: "clang-cl"
arg: "/TP"
arg: "-fprofile-instr-generate"
arg: "-fcoverage-mapping"
arg: "/FoC:\\src\\goma\\client\\build\\Debug\\vc\\stdafx.obj"
arg: "/IC:\\src\\goma\\client\\test\\vc\\include"
arg: "stdafx.cpp"
cwd: "C:\\src\\goma\\client\\test\\vc"
Input {
  filename: "C:\\src\\goma\\client\\test\\vc\\stdafx.cpp"
  hash_key: "152d72ea117deff2af0cf0ca3aaa46a20a5f0c0e4ccb8b6d559d507401ae81e9"
})###";

  const std::vector<string> kTestOptions{
      "Xclang", "B", "I", "gcc-toolchain", "-sysroot", "resource-dir"};

  devtools_goma::ExecReq req, req_expected;
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReq, &req));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req));
  ASSERT_TRUE(TextFormat::ParseFromString(kExecReqExpected, &req_expected));
  ASSERT_TRUE(devtools_goma::VerifyExecReq(req_expected));

  devtools_goma::NormalizeExecReqForCacheKey(0, true, false, kTestOptions,
                                             std::map<string, string>(), &req);

  MessageDifferencer differencer;
  string difference_reason;
  differencer.ReportDifferencesToString(&difference_reason);
  EXPECT_TRUE(differencer.Compare(req_expected, req)) << difference_reason;
}

}  // namespace devtools_goma
