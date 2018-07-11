// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "clang_tidy_compiler_info_builder.h"

#include "gtest/gtest.h"

namespace devtools_goma {

TEST(ClangTidyCompilerInfoBuilderTest, ParseClangTidyVersionTarget) {
  const char kOutput[] =
      "LLVM (http://llvm.org/):\n"
      "  LLVM version 3.9.0svn\n"
      "  Optimized build.\n"
      "  Default target: x86_64-unknown-linux-gnu\n"
      "  Host CPU: sandybridge\n";

  string version;
  string target;
  ClangTidyCompilerInfoBuilder::ParseClangTidyVersionTarget(kOutput, &version,
                                                            &target);

  EXPECT_EQ("3.9.0svn", version);
  EXPECT_EQ("x86_64-unknown-linux-gnu", target);
}

TEST(ClangTidyCompilerInfoBuilderTest, ParseClangTidyVersionTargetCRLF) {
  const char kOutput[] =
      "LLVM (http://llvm.org/):\r\n"
      "  LLVM version 3.9.0svn\r\n"
      "  Optimized build.\r\n"
      "  Default target: x86_64-unknown-linux-gnu\r\n"
      "  Host CPU: sandybridge\r\n";

  string version;
  string target;
  ClangTidyCompilerInfoBuilder::ParseClangTidyVersionTarget(kOutput, &version,
                                                            &target);

  EXPECT_EQ("3.9.0svn", version);
  EXPECT_EQ("x86_64-unknown-linux-gnu", target);
}

}  // namespace devtools_goma
