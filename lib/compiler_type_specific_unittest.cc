// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_type_specific.h"

#include "gtest/gtest.h"
using std::string;

namespace devtools_goma {

TEST(CompilerTypeSpecificTest, CompilerTypeFromArg) {
  const struct TestCase {
    CompilerType expected;
    string arg;
  } testcases[] = {
      {CompilerType::Gcc, "gcc"},
      {CompilerType::Gcc, "gcc.exe"},
      {CompilerType::Gcc, "/usr/bin/gcc"},
      {CompilerType::Gcc, "x86_64-linux-gnu-gcc"},

      {CompilerType::Gcc, "g++"},
      {CompilerType::Gcc, "g++.exe"},
      {CompilerType::Gcc, "/usr/bin/g++"},
      {CompilerType::Gcc, "x86_64-linux-gnu-g++"},

      {CompilerType::Gcc, "nacl-gcc"},
      {CompilerType::Gcc, "nacl-gcc.exe"},
      {CompilerType::Gcc, "i686-nacl-gcc"},
      {CompilerType::Gcc, "i686-nacl-gcc.exe"},
      {CompilerType::Gcc, "nacl-g++"},
      {CompilerType::Gcc, "nacl-g++.exe"},
      {CompilerType::Gcc, "i686-nacl-g++"},
      {CompilerType::Gcc, "i686-nacl-g++.exe"},
      {CompilerType::Unknown, "nacl.exe"},
      {CompilerType::Unknown,
       "D:\\nacl_sdk\\pepper_18\\toolchain\\win_x86_newlib\\bin\\nacl.exe"},

      {CompilerType::Gcc, "clang"},
      {CompilerType::Gcc, "clang.exe"},
      {CompilerType::Gcc, "/usr/local/bin/clang"},
      {CompilerType::Gcc, "pnacl-clang"},
      {CompilerType::Gcc, "pnacl-clang.exe"},
      {CompilerType::Gcc, "clang++"},
      {CompilerType::Gcc, "clang++.exe"},
      {CompilerType::Gcc, "/usr/local/bin/clang++"},
      {CompilerType::Gcc, "pnacl-clang++"},
      {CompilerType::Gcc, "pnacl-clang++.exe"},
      {CompilerType::Unknown, "clang-tblgen"},

      {CompilerType::Clexe, "cl"},
      {CompilerType::Clexe, "CL"},
      {CompilerType::Clexe, "cl.exe"},
      {CompilerType::Clexe, "CL.EXE"},
      {CompilerType::Clexe, "C:\\VS10\\VC\\bin\\cl.exe"},
      {CompilerType::Clexe,
       "D:\\Program Files\\Microsoft Visual Studio 10\\VC\\bin\\Cl.Exe"},
      {CompilerType::Clexe, "D:\\VS9\\cl.exe\\cl.exe"},
      {CompilerType::Unknown, "cl.exe.manifest"},
      {CompilerType::Unknown, "D:\\VS9\\cl.exe\\cl.exe.manifest"},
      {CompilerType::Unknown, "D:\\VS9\\cl.exe\\"},

      {CompilerType::Clexe, "clang-cl"},
      {CompilerType::Clexe, "clang-cl.exe"},
      {CompilerType::Clexe, "CLANG-CL.EXE"},
      {CompilerType::Clexe, "C:\\somewhere\\clang-cl"},
      {CompilerType::Clexe, "D:\\Program Files\\Visual Studio\\cl.exe"},
      {CompilerType::Unknown, "D:\\Program Files\\Visual Studio\\cl.exe\\"},
      {CompilerType::Unknown, "cl.clang-cl.exe"},
      {CompilerType::Unknown, "pnacl-clang-cl.exe"},

      {CompilerType::ClangTidy, "clang-tidy"},
      {CompilerType::ClangTidy, "/usr/bin/clang-tidy"},

      {CompilerType::Javac, "javac"},
      {CompilerType::Javac, "/usr/bin/javac"},

      {CompilerType::Java, "java"},
      {CompilerType::Java, "/usr/bin/java"},
  };

  for (const auto& tc : testcases) {
    EXPECT_EQ(tc.expected, CompilerTypeSpecific::FromArg(tc.arg).type())
        << "arg=" << tc.arg;
  }
}

}  // namespace devtools_goma
