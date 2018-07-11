// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc_compiler_info_builder.h"

#include "cxx/cxx_compiler_info.h"
#include "gtest/gtest.h"
#include "mypath.h"
#include "path.h"

namespace devtools_goma {

class VCCompilerInfoBuilderTest : public testing::Test {
 protected:
  void SetUp() override { CheckTempDirectory(GetGomaTmpDir()); }

  void AppendPredefinedMacros(const string& macro, CompilerInfoData* cid) {
    cid->mutable_cxx()->set_predefined_macros(cid->cxx().predefined_macros() +
                                              macro);
  }

  int FindValue(const std::unordered_map<string, int>& map, const string& key) {
    const auto& it = map.find(key);
    if (it == map.end())
      return 0;
    return it->second;
  }

  string TestDir() {
    // This module is in out\Release.
    const std::string parent_dir = file::JoinPath(GetMyDirectory(), "..");
    const std::string top_dir = file::JoinPath(parent_dir, "..");
    return file::JoinPath(top_dir, "test");
  }
};

TEST_F(VCCompilerInfoBuilderTest, ParseVCOutput) {
  // output of "cl /nologo /Bxvcflags.exe foo.cpp".
  static const char kInputCpp[] =
    "-zm0x20000000 -il "
    "C:\\Users\\a\\AppData\\Local\\Temp\\_CL_3da4ff85 -typedil -f foo.cpp -W 1 "
    "-Ze -D_MSC_EXTENSIONS -Zp8 -ZB64 -D_INTEGRAL_MAX_BITS=64 -Gs -Ot "
    "-Fofoo.obj -pc \\:/ -Fdvc90.pdb -D_MSC_VER=1500 -D_MSC_FULL_VER=150030729 "
    "-D_MSC_BUILD=1 -D_WIN32 -D_M_IX86=600 -D_M_IX86_FP=0 -GS -GR -D_CPPRTTI "
    "-Zc:forScope -Zc:wchar_t -clrNoPureCRT -D_MT "
    "-I C:\\vs08\\VC\\ATLMFC\\INCLUDE -I C:\\vs08\\VC\\INCLUDE "
    "-I \"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\include\" "
    "-I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\"
    "INCLUDE\\\\\" -I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\"
    "VC\\ATLMFC\\INCLUDE\" "
    "-I \"C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include\"";

  // output of "cl /nologo /B1vcflags.exe foo.c".
  static const char kInputC[] =
    "-zm0x20000000 -il "
    "C:\\Users\\a\\AppData\\Local\\Temp\\_CL_212628dc -typedil -f foo.c -W 1 "
    "-Ze -D_MSC_EXTENSIONS -Zp8 -ZB64 -D_INTEGRAL_MAX_BITS=64 -Gs -Ot "
    "-Fofoo.obj -pc \\:/ -Fdvc90.pdb -D_MSC_VER=1500 -D_MSC_FULL_VER=150030729 "
    "-D_MSC_BUILD=1 -D_WIN32 -D_M_IX86=600 -D_M_IX86_FP=0 -GS -clrNoPureCRT "
    "-D_MT -I C:\\vs08\\VC\\ATLMFC\\INCLUDE -I C:\\vs08\\VC\\INCLUDE "
    "-I \"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\include\" "
    "-I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\"
    "INCLUDE\\\\\" -I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\"
    "VC\\ATLMFC\\INCLUDE\" "
    "-I \"C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include\"";

  std::unique_ptr<CompilerInfoData> info_cpp_data(new CompilerInfoData);
  AppendPredefinedMacros("#define __cplusplus\n", info_cpp_data.get());
  {
    std::vector<string> cxx_system_include_paths;
    string predefined_macros(info_cpp_data->cxx().predefined_macros());
    EXPECT_TRUE(VCCompilerInfoBuilder::ParseVCOutputString(
        kInputCpp, &cxx_system_include_paths, &predefined_macros));
    for (const auto& p : cxx_system_include_paths) {
      info_cpp_data->mutable_cxx()->add_cxx_system_include_paths(p);
    }
    info_cpp_data->mutable_cxx()->set_predefined_macros(predefined_macros);
  }

  CxxCompilerInfo info_cpp(std::move(info_cpp_data));

  std::vector<string> expected_include_paths;
  expected_include_paths.push_back("C:\\vs08\\VC\\ATLMFC\\INCLUDE");
  expected_include_paths.push_back("C:\\vs08\\VC\\INCLUDE");
  expected_include_paths.push_back(
      "C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\include");
  expected_include_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\"
      "INCLUDE\\");
  expected_include_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\"
      "VC\\ATLMFC\\INCLUDE");
  expected_include_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include");
  EXPECT_EQ(0U, info_cpp.system_include_paths().size());
  EXPECT_EQ(0U, info_cpp.system_framework_paths().size());
  EXPECT_EQ(6U, info_cpp.cxx_system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info_cpp.cxx_system_include_paths());
  EXPECT_EQ(
      "#define __cplusplus\n"
      "#define _MSC_EXTENSIONS\n"
      "#define _INTEGRAL_MAX_BITS 64\n"
      "#define _MSC_VER 1500\n"
      "#define _MSC_FULL_VER 150030729\n"
      "#define _MSC_BUILD 1\n"
      "#define _WIN32\n"
      "#define _M_IX86 600\n"
      "#define _M_IX86_FP 0\n"
      "#define _CPPRTTI\n"
      "#define _MT\n",
      info_cpp.predefined_macros());

  std::unique_ptr<CompilerInfoData> info_c_data(new CompilerInfoData);
  {
    std::vector<string> system_include_paths;
    string predefined_macros;
    EXPECT_TRUE(VCCompilerInfoBuilder::ParseVCOutputString(
        kInputC, &system_include_paths, &predefined_macros));
    for (const auto& p : system_include_paths) {
      info_c_data->mutable_cxx()->add_system_include_paths(p);
    }
    info_c_data->mutable_cxx()->set_predefined_macros(predefined_macros);
  }

  CxxCompilerInfo info_c(std::move(info_c_data));
  EXPECT_EQ(6U, info_c.system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info_c.system_include_paths());
  EXPECT_EQ(0U, info_c.system_framework_paths().size());
  EXPECT_EQ(0U, info_c.cxx_system_include_paths().size());
  EXPECT_EQ(
      "#define _MSC_EXTENSIONS\n"
      "#define _INTEGRAL_MAX_BITS 64\n"
      "#define _MSC_VER 1500\n"
      "#define _MSC_FULL_VER 150030729\n"
      "#define _MSC_BUILD 1\n"
      "#define _WIN32\n"
      "#define _M_IX86 600\n"
      "#define _M_IX86_FP 0\n"
      "#define _MT\n",
      info_c.predefined_macros());

  std::unique_ptr<CompilerInfoData> info_data(new CompilerInfoData);
  AppendPredefinedMacros("#define __cplusplus\n", info_data.get());
  {
    std::vector<string> system_include_paths;
    std::vector<string> cxx_system_include_paths;
    string predefined_macros(info_data->cxx().predefined_macros());
    EXPECT_TRUE(VCCompilerInfoBuilder::ParseVCOutputString(
        kInputCpp, &cxx_system_include_paths, &predefined_macros));
    EXPECT_TRUE(VCCompilerInfoBuilder::ParseVCOutputString(
        kInputCpp, &system_include_paths, nullptr));
    for (const auto& p : cxx_system_include_paths) {
      info_data->mutable_cxx()->add_cxx_system_include_paths(p);
    }
    for (const auto& p : system_include_paths) {
      info_data->mutable_cxx()->add_system_include_paths(p);
    }
    info_data->mutable_cxx()->set_predefined_macros(predefined_macros);
  }
  CxxCompilerInfo info(std::move(info_data));
  EXPECT_EQ(6U, info.system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info.system_include_paths());
  EXPECT_EQ(0U, info.system_framework_paths().size());
  ASSERT_EQ(6U, info.cxx_system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info.cxx_system_include_paths());
  EXPECT_EQ(
      "#define __cplusplus\n"
      "#define _MSC_EXTENSIONS\n"
      "#define _INTEGRAL_MAX_BITS 64\n"
      "#define _MSC_VER 1500\n"
      "#define _MSC_FULL_VER 150030729\n"
      "#define _MSC_BUILD 1\n"
      "#define _WIN32\n"
      "#define _M_IX86 600\n"
      "#define _M_IX86_FP 0\n"
      "#define _CPPRTTI\n"
      "#define _MT\n",
      info.predefined_macros());

  std::vector<string> dummy;
  EXPECT_FALSE(
      VCCompilerInfoBuilder::ParseVCOutputString("\"", &dummy, nullptr));
}

TEST_F(VCCompilerInfoBuilderTest, GetVCVersion) {
  static const char kVc2008[] =
    "Microsoft (R) 32-bit C/C++ Optimizing Compiler Version 15.00.30729.01 for "
    "80x86\r\nCopyright (C) Microsoft Corporation.  All rights reserved.\r\n"
    "\r\nusage: cl [ option... ] filename... [ /link linkoption... ]\r\n";

  static const char kVc2010[] =
    "Microsoft (R) 32-bit C/C++ Optimizing Compiler Version 16.00.40219.01 for "
    "80x86\r\nCopyright (C) Microsoft Corporation.  All rights reserved.\r\n"
    "\r\nusage: cl [ option... ] filename... [ /link linkoption... ]\r\n";

  static const char kWinsdk71[] =
    "Microsoft (R) C/C++ Optimizing Compiler Version 16.00.40219.01 for x64\r\n"
    "Copyright (C) Microsoft Corporation.  All rights reserved.\r\n\r\n"
    "cl : Command line error D8003 : missing source filename";

  string version, target;
  EXPECT_TRUE(
      VCCompilerInfoBuilder::ParseVCVersion(kVc2008, &version, &target));
  EXPECT_EQ("15.00.30729.01", version);
  EXPECT_EQ("80x86", target);
  version.clear();
  target.clear();
  EXPECT_TRUE(
      VCCompilerInfoBuilder::ParseVCVersion(kVc2010, &version, &target));
  EXPECT_EQ("16.00.40219.01", version);
  EXPECT_EQ("80x86", target);
  version.clear();
  target.clear();
  EXPECT_TRUE(
      VCCompilerInfoBuilder::ParseVCVersion(kWinsdk71, &version, &target));
  EXPECT_EQ("16.00.40219.01", version);
  EXPECT_EQ("x64", target);
  version.clear();
  target.clear();
  EXPECT_FALSE(VCCompilerInfoBuilder::ParseVCVersion("", &version, &target));
}

}  // namespace devtools_goma
