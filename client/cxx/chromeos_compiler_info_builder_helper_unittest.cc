// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos_compiler_info_builder_helper.h"

#include "gtest/gtest.h"

using std::string;

namespace devtools_goma {

TEST(ChromeOSCompilerInfoBuilderHelperTest, IsSimpleChromeClangCommand) {
  EXPECT_TRUE(ChromeOSCompilerInfoBuilderHelper::IsSimpleChromeClangCommand(
      "../../work/chrome-chromeos/src/build/cros_cache/chrome-sdk/tarballs/"
      "amd64-generic+11550.0.0+target_toolchain/usr/bin/clang++",
      "../../work/chrome-chromeos/src/build/cros_cache/chrome-sdk/tarballs/"
      "amd64-generic+11550.0.0+target_toolchain/usr/bin/clang-8.elf"));
}

TEST(ChromeOSCompilerInfoBuilderHelperTest, EstimateClangMajorVersion) {
  int version;

  EXPECT_TRUE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clang-7.elf", &version));
  EXPECT_EQ(7, version);
  EXPECT_TRUE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clang-8.elf", &version));
  EXPECT_EQ(8, version);
  EXPECT_TRUE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clang-10.elf", &version));
  EXPECT_EQ(10, version);

  EXPECT_FALSE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clang++-7.elf", &version));
  EXPECT_FALSE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clang++-8.elf", &version));

  EXPECT_FALSE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clang-7.elf.elf", &version));
  EXPECT_FALSE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clang-8.so", &version));
  EXPECT_FALSE(ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
      "../path/to/usr/bin/clangclang-7.elf", &version));
}

}  // namespace devtools_goma
