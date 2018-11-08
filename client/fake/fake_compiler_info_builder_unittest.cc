// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_compiler_info_builder.h"

#include "fake_compiler_info.h"
#include "fake_flags.h"
#include "gtest/gtest.h"
#include "mypath.h"
#include "path.h"
#include "subprocess.h"
#include "util.h"

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

class FakeCompilerInfoBuilderTest : public ::testing::Test {
 protected:
  FakeCompilerInfoBuilderTest() {
#ifndef _WIN32
    InstallReadCommandOutputFunc(ReadCommandOutputByPopen);
#else
    InstallReadCommandOutputFunc(ReadCommandOutputByRedirector);
#endif
  }
};

TEST_F(FakeCompilerInfoBuilderTest, Success) {
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
  ASSERT_NE(nullptr, data.get());

  FakeCompilerInfo compiler_info(std::move(data));
  EXPECT_FALSE(compiler_info.HasError()) << compiler_info.error_message();
}

TEST_F(FakeCompilerInfoBuilderTest, Failure) {
#ifndef _WIN32
  const string local_compiler_path = "/somewhere/not/exist/fake";
#else
  const string local_compiler_path = "C:\\somewhere\\not\\exist\\fake.exe";
#endif
  const std::vector<string> args{
      local_compiler_path, "foo.fake", "bar.fake",
  };
  const string cwd = GetCurrentDirNameOrDie();
  const std::vector<string> compiler_info_envs = DefaultCompilerInfoEnvs(cwd);

  FakeFlags flags(args, cwd);

  std::unique_ptr<CompilerInfoData> data =
      FakeCompilerInfoBuilder().FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
  ASSERT_NE(nullptr, data.get());
  // Even if compiler doesn't exist, CompilerInfo should have fake.
  // Otherwise, CompilerInfoState::MakeCompilerInfo would fail.
  EXPECT_TRUE(data->has_fake());
  // CompilerInfo should recognize lack of the compiler.
  EXPECT_FALSE(data->found());
}

}  // namespace devtools_goma
