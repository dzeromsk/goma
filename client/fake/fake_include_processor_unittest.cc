// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_include_processor.h"

#include <set>
#include <string>
#include <vector>

#include "fake_compiler_info.h"
#include "fake_compiler_info_builder.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"
#include "mypath.h"
#include "path.h"
#include "subprocess.h"
#include "util.h"

using std::string;

namespace devtools_goma {

namespace {

#ifndef _WIN32
const char kFakeExe[] = "fake";
#else
const char kFakeExe[] = "fake.exe";
#endif

std::vector<string> DefaultCompilerInfoEnvs(const string& cwd) {
#ifndef _WIN32
  return std::vector<string>();
#else
  // On Windows, PATH and PATHEXT must exist in compiler_info_envs.
  return std::vector<string> {
    "PATH=" + cwd,
    "PATHEXT=.exe",
  };
#endif
}

}  // namespace

class FakeIncludeProcessorTest : public ::testing::Test {
 protected:
  FakeIncludeProcessorTest() {
#ifndef _WIN32
    InstallReadCommandOutputFunc(ReadCommandOutputByPopen);
#else
    InstallReadCommandOutputFunc(ReadCommandOutputByRedirector);
#endif
  }
};

TEST_F(FakeIncludeProcessorTest, Success) {
  // Assuming `fake` exists the same directory with this unittest.
  const std::vector<string> args{
      kFakeExe, "foo.fake", "bar.fake",
  };
  const string cwd = GetCurrentDirNameOrDie();
  const string local_compiler_path = file::JoinPath(cwd, kFakeExe);
  const std::vector<string> compiler_info_envs = DefaultCompilerInfoEnvs(cwd);

  FakeFlags flags(args, cwd);

  std::unique_ptr<CompilerInfoData> data =
      FakeCompilerInfoBuilder().FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
  ASSERT_TRUE(data.get() != nullptr);

  FakeCompilerInfo compiler_info(std::move(data));
  EXPECT_FALSE(compiler_info.HasError()) << compiler_info.error_message();

  FakeIncludeProcessor include_processor;
  std::set<string> required_files;
  EXPECT_TRUE(
      include_processor.Run("trace_id", flags, compiler_info, &required_files));

  EXPECT_EQ((std::set<string>{"success.txt"}), required_files);
}

TEST_F(FakeIncludeProcessorTest, Failure) {
  // If input filename is "fail", "fake"'s include processor fails.
  const std::vector<string> args{
      kFakeExe, "fail",
  };
  const string cwd = GetCurrentDirNameOrDie();
  const string local_compiler_path = file::JoinPath(cwd, kFakeExe);
  const std::vector<string> compiler_info_envs = DefaultCompilerInfoEnvs(cwd);

  FakeFlags flags(args, cwd);

  std::unique_ptr<CompilerInfoData> data =
      FakeCompilerInfoBuilder().FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
  ASSERT_TRUE(data.get() != nullptr);

  FakeCompilerInfo compiler_info(std::move(data));
  EXPECT_FALSE(compiler_info.HasError()) << compiler_info.error_message();

  FakeIncludeProcessor include_processor;
  std::set<string> required_files;
  EXPECT_FALSE(
      include_processor.Run("trace_id", flags, compiler_info, &required_files));
}

}  // namespace devtools_goma
