// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gcc_compiler_info_builder.h"

#include "gtest/gtest.h"
#include "mypath.h"
#include "path.h"
#include "subprocess.h"
#include "unittest_util.h"
#include "util.h"

namespace devtools_goma {

class GCCCompilerInfoBuilderTest : public testing::Test {
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
    const string parent_dir = file::JoinPath(GetMyDirectory(), "..");
    const string top_dir = file::JoinPath(parent_dir, "..");
    return file::JoinPath(top_dir, "test");
  }
};

TEST_F(GCCCompilerInfoBuilderTest, GetExtraSubprogramsClangPlugin) {
  const string cwd("/");

  TmpdirUtil tmpdir("get_extra_subprograms_clang_plugin");
  tmpdir.SetCwd(cwd);
  tmpdir.CreateEmptyFile("libPlugin.so");

  std::vector<string> args, envs;
  args.push_back("/usr/bin/clang");
  args.push_back("-Xclang");
  args.push_back("-load");
  args.push_back("-Xclang");
  args.push_back(file::JoinPath(tmpdir.tmpdir(), "libPlugin.so"));
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, cwd);
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  GCCCompilerInfoBuilder::ParseSubprogramFlags(
      "/usr/bin/clang", flags, &clang_plugins, &B_options, &no_integrated_as);
  std::vector<string> expected = {tmpdir.FullPath("libPlugin.so")};
  EXPECT_EQ(expected, clang_plugins);
  EXPECT_TRUE(B_options.empty());
  EXPECT_FALSE(no_integrated_as);
}

TEST_F(GCCCompilerInfoBuilderTest, GetExtraSubprogramsClangPluginRelative) {
  const string cwd("/");

  TmpdirUtil tmpdir("get_extra_subprograms_clang_plugin");
  tmpdir.SetCwd(cwd);
  tmpdir.CreateEmptyFile("libPlugin.so");

  std::vector<string> args, envs;
  args.push_back("/usr/bin/clang");
  args.push_back("-Xclang");
  args.push_back("-load");
  args.push_back("-Xclang");
  args.push_back("libPlugin.so");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, cwd);
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  GCCCompilerInfoBuilder::ParseSubprogramFlags(
      "/usr/bin/clang", flags, &clang_plugins, &B_options, &no_integrated_as);
  std::vector<string> expected = {"libPlugin.so"};
  EXPECT_EQ(expected, clang_plugins);
  EXPECT_TRUE(B_options.empty());
  EXPECT_FALSE(no_integrated_as);
}

TEST_F(GCCCompilerInfoBuilderTest, GetExtraSubprogramsBOptions) {
  const string cwd("/");

  TmpdirUtil tmpdir("get_extra_subprograms_clang_plugin");
  tmpdir.SetCwd(cwd);
  tmpdir.CreateEmptyFile("libPlugin.so");

  std::vector<string> args, envs;
  args.push_back("/usr/bin/clang");
  args.push_back("-B");
  args.push_back("dummy");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, cwd);
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  GCCCompilerInfoBuilder::ParseSubprogramFlags(
      "/usr/bin/clang", flags, &clang_plugins, &B_options, &no_integrated_as);
  std::vector<string> expected = {"dummy"};
  EXPECT_TRUE(clang_plugins.empty());
  EXPECT_EQ(expected, B_options);
  EXPECT_FALSE(no_integrated_as);
}

TEST_F(GCCCompilerInfoBuilderTest, GetCompilerNameUsualCases) {
  std::vector<std::pair<string, string>> test_cases = {
      {"clang", "clang"}, {"clang++", "clang"}, {"g++", "g++"}, {"gcc", "gcc"},
  };

  GCCCompilerInfoBuilder builder;
  for (const auto& tc : test_cases) {
    CompilerInfoData data;
    data.set_local_compiler_path(tc.first);
    data.set_real_compiler_path(tc.second);
    EXPECT_EQ(tc.first, builder.GetCompilerName(data));
  }
}

TEST_F(GCCCompilerInfoBuilderTest, GetCompilerNameCc) {
  std::vector<string> test_cases = {"clang", "gcc"};

  GCCCompilerInfoBuilder builder;
  for (const auto& tc : test_cases) {
    CompilerInfoData data;
    data.set_local_compiler_path("cc");
    data.set_real_compiler_path(tc);
    EXPECT_EQ(tc, builder.GetCompilerName(data));
  }
}

TEST_F(GCCCompilerInfoBuilderTest, GetCompilerNameCxx) {
  GCCCompilerInfoBuilder builder;
  CompilerInfoData data;

  data.set_local_compiler_path("c++");
  data.set_real_compiler_path("g++");
  EXPECT_EQ("g++", builder.GetCompilerName(data));

  data.set_local_compiler_path("c++");
  data.set_real_compiler_path("clang");
  EXPECT_EQ("clang++", builder.GetCompilerName(data));
}

TEST_F(GCCCompilerInfoBuilderTest, GetCompilerNameUnsupportedCase) {
  GCCCompilerInfoBuilder builder;

  CompilerInfoData data;
  data.set_local_compiler_path("c++");
  data.set_real_compiler_path("clang++");
  EXPECT_EQ("", builder.GetCompilerName(data));
}

#ifndef _WIN32
// Since we use real clang and subprogram, use non-Win env only.
TEST_F(GCCCompilerInfoBuilderTest, BuildWithRealClang) {
  InstallReadCommandOutputFunc(ReadCommandOutputByPopen);

  TmpdirUtil tmpdir("build_with_real_clang");
  tmpdir.SetCwd("");

  // clang++ is usually a symlink to clang.
  // To check a symlink is correctly working, use clang++ instead of clang.

  const string clang = GetClangPath();
  // TODO: unittest_util should have GetClangXXPath()?
  const string clangxx = GetClangPath() + "++";

  // Needs to use real .so otherwise clang fails to read the file.
  // Linux has .so, and mac has .dylib.
  // TODO: Remove plugin use? (b/122436038)
#ifdef __MACH__
  const string lib_find_bad_constructs_so = file::JoinPath(
      file::Dirname(clangxx), "..", "lib", "libFindBadConstructs.dylib");
#else
  const string lib_find_bad_constructs_so = file::JoinPath(
      file::Dirname(clangxx), "..", "lib", "libFindBadConstructs.so");
#endif

  std::vector<string> args{
      clangxx,
      "-c",
      "hello.cc",
  };

  if (access(lib_find_bad_constructs_so.c_str(), R_OK) == 0) {
    const std::vector<string> plugin_args = {"-Xclang", "-load", "-Xclang",
                                             lib_find_bad_constructs_so};
    args.insert(args.end(), plugin_args.begin(), plugin_args.end());
  }

  const std::vector<string> envs;
  GCCFlags flags(args, tmpdir.realcwd());

  GCCCompilerInfoBuilder builder;
  std::unique_ptr<CompilerInfoData> data =
      builder.FillFromCompilerOutputs(flags, clangxx, envs);

  std::vector<string> actual_executable_binaries;
  ASSERT_NE(data.get(), nullptr);
  for (const auto& resource : data->resource()) {
    if (resource.type() == CompilerInfoData_ResourceType_EXECUTABLE_BINARY) {
      actual_executable_binaries.push_back(resource.name());
    }
  }

  std::vector<string> expected_executable_binaries{
      clangxx,
      clang,
  };

  if (access(lib_find_bad_constructs_so.c_str(), R_OK) == 0) {
    expected_executable_binaries.push_back(lib_find_bad_constructs_so);
  }

  std::sort(expected_executable_binaries.begin(),
            expected_executable_binaries.end());
  std::sort(actual_executable_binaries.begin(),
            actual_executable_binaries.end());
  EXPECT_EQ(expected_executable_binaries, actual_executable_binaries);
}
#endif

}  // namespace devtools_goma
