// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/compiler_flag_type_specific.h"

#include "gtest/gtest.h"
using std::string;

namespace devtools_goma {

TEST(CompilerFlagTypeSpecificTest, CompilerFlagTypeAndCompilerName) {
  const struct TestCase {
    CompilerFlagType compiler_type_expected;
    string compiler_name_expected;
    string arg;
  } testcases[] = {
      {CompilerFlagType::Gcc, "gcc", "gcc"},
      {CompilerFlagType::Gcc, "gcc", "gcc.exe"},
      {CompilerFlagType::Gcc, "gcc", "/usr/bin/gcc"},
      {CompilerFlagType::Gcc, "gcc", "x86_64-linux-gnu-gcc"},

      {CompilerFlagType::Gcc, "g++", "g++"},
      {CompilerFlagType::Gcc, "g++", "g++.exe"},
      {CompilerFlagType::Gcc, "g++", "/usr/bin/g++"},
      {CompilerFlagType::Gcc, "g++", "x86_64-linux-gnu-g++"},

      {CompilerFlagType::Gcc, "gcc", "nacl-gcc"},
      {CompilerFlagType::Gcc, "gcc", "nacl-gcc.exe"},
      {CompilerFlagType::Gcc, "gcc", "i686-nacl-gcc"},
      {CompilerFlagType::Gcc, "gcc", "i686-nacl-gcc.exe"},
      {CompilerFlagType::Gcc, "g++", "nacl-g++"},
      {CompilerFlagType::Gcc, "g++", "nacl-g++.exe"},
      {CompilerFlagType::Gcc, "g++", "i686-nacl-g++"},
      {CompilerFlagType::Gcc, "g++", "i686-nacl-g++.exe"},
      {CompilerFlagType::Unknown, "", "nacl.exe"},
      {CompilerFlagType::Unknown, "",
       "D:\\nacl_sdk\\pepper_18\\toolchain\\win_x86_newlib\\bin\\nacl.exe"},

      {CompilerFlagType::Gcc, "clang", "clang"},
      {CompilerFlagType::Gcc, "clang", "clang.exe"},
      {CompilerFlagType::Gcc, "clang", "/usr/local/bin/clang"},
      {CompilerFlagType::Gcc, "clang", "pnacl-clang"},
      {CompilerFlagType::Gcc, "clang", "pnacl-clang.exe"},
      {CompilerFlagType::Gcc, "clang++", "clang++"},
      {CompilerFlagType::Gcc, "clang++", "clang++.exe"},
      {CompilerFlagType::Gcc, "clang++", "/usr/local/bin/clang++"},
      {CompilerFlagType::Gcc, "clang++", "pnacl-clang++"},
      {CompilerFlagType::Gcc, "clang++", "pnacl-clang++.exe"},
      {CompilerFlagType::Unknown, "", "clang-tblgen"},

      {CompilerFlagType::Clexe, "cl.exe", "cl"},
      {CompilerFlagType::Clexe, "cl.exe", "CL"},
      {CompilerFlagType::Clexe, "cl.exe", "cl.exe"},
      {CompilerFlagType::Clexe, "cl.exe", "CL.EXE"},
      {CompilerFlagType::Clexe, "cl.exe", "C:\\VS10\\VC\\bin\\cl.exe"},
      {CompilerFlagType::Clexe, "cl.exe",
       "D:\\Program Files\\Microsoft Visual Studio 10\\VC\\bin\\Cl.Exe"},
      {CompilerFlagType::Clexe, "cl.exe", "D:\\VS9\\cl.exe\\cl.exe"},
      {CompilerFlagType::Unknown, "", "cl.exe.manifest"},
      {CompilerFlagType::Unknown, "", "D:\\VS9\\cl.exe\\cl.exe.manifest"},
      {CompilerFlagType::Unknown, "", "D:\\VS9\\cl.exe\\"},

      {CompilerFlagType::Clexe, "clang-cl", "clang-cl"},
      {CompilerFlagType::Clexe, "clang-cl", "clang-cl.exe"},
      {CompilerFlagType::Clexe, "clang-cl", "CLANG-CL.EXE"},
      {CompilerFlagType::Clexe, "clang-cl", "C:\\somewhere\\clang-cl"},
      {CompilerFlagType::Clexe, "clang-cl",
       "D:\\Program Files\\Visual Studio\\clang-cl.exe"},
      {CompilerFlagType::Unknown, "",
       "D:\\Program Files\\Visual Studio\\cl.exe\\"},
      {CompilerFlagType::Unknown, "", "cl.clang-cl.exe"},
      {CompilerFlagType::Unknown, "", "pnacl-clang-cl.exe"},

      {CompilerFlagType::ClangTidy, "clang-tidy", "clang-tidy"},
      {CompilerFlagType::ClangTidy, "clang-tidy", "/usr/bin/clang-tidy"},

      {CompilerFlagType::Javac, "javac", "javac"},
      {CompilerFlagType::Javac, "javac", "/usr/bin/javac"},

      {CompilerFlagType::Java, "java", "java"},
      {CompilerFlagType::Java, "java", "/usr/bin/java"},
  };

  for (const auto& tc : testcases) {
    EXPECT_EQ(tc.compiler_type_expected,
              CompilerFlagTypeSpecific::FromArg(tc.arg).type())
        << "arg=" << tc.arg;
    EXPECT_EQ(tc.compiler_name_expected,
              CompilerFlagTypeSpecific::FromArg(tc.arg).GetCompilerName(tc.arg))
        << "arg=" << tc.arg;
  }
}

}  // namespace devtools_goma
