// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "clang_compiler_info_builder_helper.h"

#include <string>
#include <unordered_map>

#include "absl/base/macros.h"
#include "cxx_compiler_info.h"
#include "gtest/gtest.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

namespace {

int FindValue(const std::unordered_map<string, int>& map, const string& key) {
  const auto& it = map.find(key);
  if (it == map.end())
    return 0;
  return it->second;
}

}  // anonymous namespace

#ifndef _WIN32

TEST(ClangCompilerInfoBuilderHelperTest, ParseResourceOutputPosix) {
  static const char kDummyClangOutput[] =
      "Fuchsia clang version 7.0.0\n"
      "Target: x86_64-unknown-linux-gnu\n"
      "Thread model: posix\n"
      "InstalledDir: /bin\n"
      "Found candidate GCC installation: gcc/x86_64-linux-gnu/4.6\n"
      "Selected GCC installation: gcc/x86_64-linux-gnu/4.6\n"
      "Candidate multilib: .;@m64\n"
      "Selected multilib: .;@m64\n"
      " \"/third_party/llvm-build/Release+Asserts/bin/clang\" -cc1 -triple "
      "x86_64-unknown-linux-gnu -emit-obj -mrelax-all -disable-free "
      "-main-file-name null -mrelocation-model static -mthread-model posix "
      "-mdisable-fp-elim -fmath-errno -masm-verbose -mconstructor-aliases "
      "-munwind-tables -fuse-init-array -target-cpu x86-64 "
      "-dwarf-column-info -debugger-tuning=gdb -v -coverage-notes-file "
      "/dev/null.gcno -resource-dir "
      "/third_party/llvm-build/Release+Asserts/lib/clang/7.0.0 "
      "-internal-isystem /usr/local/include -internal-isystem "
      "/third_party/llvm-build/Release+Asserts/lib/clang/7.0.0/include "
      "-internal-externc-isystem /usr/include/x86_64-linux-gnu "
      "-internal-externc-isystem /include -internal-externc-isystem "
      "/usr/include -ferror-limit 19 -fmessage-length 80 -fsanitize=address "
      "-fsanitize-blacklist=/third_party/llvm-build/Release+Asserts/lib/clang"
      "/7.0.0/share/asan_blacklist.txt -fsanitize-address-use-after-scope "
      "-fno-assume-sane-operator-new -fobjc-runtime=gcc "
      "-fdiagnostics-show-option -fcolor-diagnostics -o /dev/null -x c "
      "/dev/null";
  TmpdirUtil tmpdir("parse_resource_output");
  tmpdir.CreateEmptyFile("gcc/x86_64-linux-gnu/4.6/crtbegin.o");
  std::vector<ClangCompilerInfoBuilderHelper::ResourceList> resource;
  EXPECT_EQ(ClangCompilerInfoBuilderHelper::ParseStatus::kSuccess,
            ClangCompilerInfoBuilderHelper::ParseResourceOutput(
                "/third_party/llvm-build/Release+Asserts/bin/clang",
                tmpdir.realcwd(), kDummyClangOutput, &resource));
  std::vector<ClangCompilerInfoBuilderHelper::ResourceList> expected = {
      {"gcc/x86_64-linux-gnu/4.6/crtbegin.o",
       CompilerInfoData::CLANG_GCC_INSTALLATION_MARKER},
      {"/third_party/llvm-build/Release+Asserts/lib/clang"
       "/7.0.0/share/asan_blacklist.txt",
       CompilerInfoData::CLANG_RESOURCE},
  };
  EXPECT_EQ(expected, resource);
}

TEST(ClangCompilerInfoBuilderHelperTest, ParseResourceOutputPosixMultilib) {
  // $ /path/to/goma/clang -m32 -v -E -o /dev/null -x c /dev/null
  // and modified GCC installation path (remove /usr/lib), and search
  // directories
  static const char kDummyClangOutput[] =
      R"(clang version 8.0.0 (trunk 340925)
Target: i386-unknown-linux-gnu
Thread model: posix
InstalledDir: /home/goma/work/goma-client/client/third_party/llvm-build/Release+Asserts/bin
Found candidate GCC installation: gcc/i686-linux-gnu/6.4.0
Found candidate GCC installation: gcc/i686-linux-gnu/7
Found candidate GCC installation: gcc/i686-linux-gnu/7.3.0
Found candidate GCC installation: gcc/i686-linux-gnu/8
Found candidate GCC installation: gcc/i686-linux-gnu/8.0.1
Found candidate GCC installation: gcc/x86_64-linux-gnu/6
Found candidate GCC installation: gcc/x86_64-linux-gnu/6.4.0
Found candidate GCC installation: gcc/x86_64-linux-gnu/7
Found candidate GCC installation: gcc/x86_64-linux-gnu/7.3.0
Found candidate GCC installation: gcc/x86_64-linux-gnu/8
Found candidate GCC installation: gcc/x86_64-linux-gnu/8.0.1
Selected GCC installation: gcc/x86_64-linux-gnu/7.3.0
Candidate multilib: .;@m64
Candidate multilib: 32;@m32
Candidate multilib: x32;@mx32
Selected multilib: 32;@m32
 "/home/goma/work/goma-client/client/third_party/llvm-build/Release+Asserts/bin/clang" -cc1 -triple i386-unknown-linux-gnu -E -disable-free -main-file-name null -mrelocation-model static -mthread-model posix -mdisable-fp-elim -fmath-errno -masm-verbose -mconstructor-aliases -fuse-init-array -target-cpu pentium4 -dwarf-column-info -debugger-tuning=gdb -v -resource-dir /third_party/llvm-build/Release+Asserts/lib/clang/8.0.0 -internal-isystem /usr/local/include -internal-isystem /third_party/llvm-build/Release+Asserts/lib/clang/8.0.0/include -internal-externc-isystem /usr/include/i386-linux-gnu -internal-externc-isystem /include -internal-externc-isystem /usr/include -fdebug-compilation-dir /tmp -ferror-limit 19 -fmessage-length 115 -fobjc-runtime=gcc -fdiagnostics-show-option -fcolor-diagnostics -o /dev/null -x c /dev/null -faddrsig
clang -cc1 version 8.0.0 based upon LLVM 8.0.0svn default target x86_64-unknown-linux-gnu
)";

  TmpdirUtil tmpdir("parse_resource_output");
  tmpdir.CreateEmptyFile("gcc/x86_64-linux-gnu/7.3.0/crtbegin.o");
  tmpdir.CreateEmptyFile("gcc/x86_64-linux-gnu/7.3.0/32/crtbegin.o");
  tmpdir.CreateEmptyFile("gcc/x86_64-linux-gnu/7.3.0/x32/crtbegin.o");
  std::vector<ClangCompilerInfoBuilderHelper::ResourceList> resource;
  EXPECT_EQ(ClangCompilerInfoBuilderHelper::ParseStatus::kSuccess,
            ClangCompilerInfoBuilderHelper::ParseResourceOutput(
                "/third_party/llvm-build/Release+Asserts/bin/clang",
                tmpdir.realcwd(), kDummyClangOutput, &resource));
  std::vector<ClangCompilerInfoBuilderHelper::ResourceList> expected = {
      {"gcc/x86_64-linux-gnu/7.3.0/crtbegin.o",
       CompilerInfoData::CLANG_GCC_INSTALLATION_MARKER},
      {"gcc/x86_64-linux-gnu/7.3.0/32/crtbegin.o",
       CompilerInfoData::CLANG_GCC_INSTALLATION_MARKER},
      {"gcc/x86_64-linux-gnu/7.3.0/x32/crtbegin.o",
       CompilerInfoData::CLANG_GCC_INSTALLATION_MARKER},
  };
  EXPECT_EQ(expected, resource);
}

TEST(ClangCompilerInfoBuilderHelperTest, GetResourceDirPosix) {
  static const char kDummyClangOutput[] =
      "Fuchsia clang version 7.0.0\n"
      "Target: x86_64-unknown-linux-gnu\n"
      "Thread model: posix\n"
      "InstalledDir: /bin\n"
      "Found candidate GCC installation: gcc/x86_64-linux-gnu/4.6\n"
      "Selected GCC installation: gcc/x86_64-linux-gnu/4.6\n"
      "Candidate multilib: .;@m64\n"
      "Selected multilib: .;@m64\n"
      " \"/third_party/llvm-build/Release+Asserts/bin/clang\" -cc1 -triple "
      "x86_64-unknown-linux-gnu -emit-obj -mrelax-all -disable-free "
      "-main-file-name null -mrelocation-model static -mthread-model posix "
      "-mdisable-fp-elim -fmath-errno -masm-verbose -mconstructor-aliases "
      "-munwind-tables -fuse-init-array -target-cpu x86-64 "
      "-dwarf-column-info -debugger-tuning=gdb -v -coverage-notes-file "
      "/dev/null.gcno -resource-dir "
      "/third_party/llvm-build/Release+Asserts/lib/clang/7.0.0 "
      "-internal-isystem /usr/local/include -internal-isystem "
      "/third_party/llvm-build/Release+Asserts/lib/clang/7.0.0/include "
      "-internal-externc-isystem /usr/include/x86_64-linux-gnu "
      "-internal-externc-isystem /include -internal-externc-isystem "
      "/usr/include -ferror-limit 19 -fmessage-length 80 -fsanitize=address "
      "-fsanitize-blacklist=/third_party/llvm-build/Release+Asserts/lib/clang"
      "/7.0.0/share/asan_blacklist.txt -fsanitize-address-use-after-scope "
      "-fno-assume-sane-operator-new -fobjc-runtime=gcc "
      "-fdiagnostics-show-option -fcolor-diagnostics -o /dev/null -x c "
      "/dev/null";
  CompilerInfoData compiler_info;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::GetResourceDir(kDummyClangOutput,
                                                             &compiler_info));
  EXPECT_EQ("/third_party/llvm-build/Release+Asserts/lib/clang/7.0.0",
            compiler_info.cxx().resource_dir());
}

TEST(ClangCompilerInfoBuilderHelperTest, GetResourceDirPosixClangCl) {
  static const char kDummyClangClOutput[] =
      "clang version 7.0.0 (trunk 332838)\n"
      "Target: x86_64-pc-windows-msvc\n"
      "Thread model: posix\n"
      "InstalledDir: ../../third_party/llvm-build/Release+Asserts/bin\n"
      " \"../../third_party/llvm-build/Release+Asserts/bin/clang\" -cc1 -"
      "triple x86_64-pc-windows-msvc19.11.0 -emit-obj -mrelax-all -mincre"
      "mental-linker-compatible -disable-free -main-file-name empty.cc -m"
      "relocation-model pic -pic-level 2 -mthread-model posix -relaxed-al"
      "iasing -fmath-errno -masm-verbose -mconstructor-aliases -munwind-t"
      "ables -target-cpu x86-64 -mllvm -x86-asm-syntax=intel -D_MT -flto-"
      "visibility-public-std --dependent-lib=libcmt --dependent-lib=oldna"
      "mes -stack-protector 2 -fms-volatile -fdiagnostics-format msvc -dw"
      "arf-column-info -debugger-tuning=gdb -momit-leaf-frame-pointer -v "
      "-coverage-notes-file ../../empty.gcno -resource-dir ../../third_pa"
      "rty/llvm-build/Release+Asserts/lib/clang/7.0.0 -internal-isystem ."
      "./../third_party/llvm-build/Release+Asserts/lib/clang/7.0.0/includ"
      "e -fdeprecated-macro -fdebug-compilation-dir ../.. -ferror-limit 1"
      "9 -fmessage-length 0 -fsanitize=address -fsanitize-blacklist=../.."
      "/third_party/llvm-build/Release+Asserts/lib/clang/7.0.0/share/asan"
      "_blacklist.txt -fsanitize-address-use-after-scope -fsanitize-addre"
      "ss-globals-dead-stripping -fno-assume-sane-operator-new -fno-use-c"
      "xa-atexit -fms-extensions -fms-compatibility -fms-compatibility-ve"
      "rsion=19.11 -std=c++14 -fdelayed-template-parsing -fobjc-runtime=g"
      "cc -fseh-exceptions -fdiagnostics-show-option -o empty.obj -x c++ "
      "/tmp/empty.cc\n";
  CompilerInfoData compiler_info;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::GetResourceDir(
      kDummyClangClOutput, &compiler_info));
  EXPECT_EQ("../../third_party/llvm-build/Release+Asserts/lib/clang/7.0.0",
            compiler_info.cxx().resource_dir());
}
#else
TEST(ClangCompilerInfoBuilderHelperTest, ParseResourceOutputWin) {
  static const char kDummyClangOutput[] =
      "clang version 7.0.0 (trunk 332838)\n"
      "Target: x86_64-pc-windows-msvc\n"
      "Thread model: posix\n"
      "InstalledDir: c:\\third_party\\llvm-build\\Release+Asserts\\bin\n"
      " \"c:\\\\third_party\\\\llvm-build\\\\Release+Asserts\\\\"
      "bin\\\\clang-cl.exe\" \"-cc1\" \"-triple\" "
      "\"x86_64-pc-windows-msvc19.11.0\" \"-emit-obj\" \"-mrelax-all\" "
      "\"-mincremental-linker-compatible\" \"-disable-free\" "
      "\"-ferror-limit\" \"19\" \"-fmessage-length\" \"89\" "
      "\"-resource-dir\" \"c:\\\\third_party\\\\llvm-build\\\\"
      "Release+Asserts\\\\lib\\\\clang\\\\7.0.0\" "
      "\"-fsanitize=address\" \"-fsanitize-blacklist=c:\\\\third_party"
      "\\\\llvm-build\\\\Release+Asserts\\\\lib\\\\clang\\\\7.0.0"
      "\\\\share\\\\asan_blacklist.txt\" \"-fsanitize-address-use-after-scope\""
      "\"-fms-compatibility\" \"-fms-compatibility-version=19.11\"";
  std::vector<ClangCompilerInfoBuilderHelper::ResourceList> resource;
  EXPECT_EQ(ClangCompilerInfoBuilderHelper::ParseStatus::kSuccess,
            ClangCompilerInfoBuilderHelper::ParseResourceOutput(
                "c:\\third_party\\llvm-build\\Release+Asserts\\"
                "bin\\clang-cl.exe",
                ".", kDummyClangOutput, &resource));
  std::vector<ClangCompilerInfoBuilderHelper::ResourceList> expected = {
      {"c:\\\\third_party\\\\llvm-build\\\\Release+Asserts\\\\lib\\\\clang"
       "\\\\7.0.0\\\\share\\\\asan_blacklist.txt",
       CompilerInfoData::CLANG_RESOURCE},
  };
  EXPECT_EQ(expected, resource);
}

TEST(ClangCompilerInfoBuilderHelperTest, GetResourceDirWinClangCl) {
  static const char kDummyClangOutput[] =
      "clang version 7.0.0 (trunk 332838)\n"
      "Target: x86_64-pc-windows-msvc\n"
      "Thread model: posix\n"
      "InstalledDir: c:\\third_party\\llvm-build\\Release+Asserts\\bin\n"
      " \"c:\\\\third_party\\\\llvm-build\\\\Release+Asserts\\\\"
      "bin\\\\clang-cl.exe\" \"-cc1\" \"-triple\" "
      "\"x86_64-pc-windows-msvc19.11.0\" \"-emit-obj\" \"-mrelax-all\" "
      "\"-mincremental-linker-compatible\" \"-disable-free\" "
      "\"-ferror-limit\" \"19\" \"-fmessage-length\" \"89\" "
      "\"-resource-dir\" \"c:\\\\third_party\\\\llvm-build\\\\"
      "Release+Asserts\\\\lib\\\\clang\\\\7.0.0\" "
      "\"-fsanitize=address\" \"-fsanitize-blacklist=c:\\\\third_party"
      "\\\\llvm-build\\\\Release+Asserts\\\\lib\\\\clang\\\\7.0.0"
      "\\\\share\\\\asan_blacklist.txt\" \"-fsanitize-address-use-after-scope\""
      "\"-fms-compatibility\" \"-fms-compatibility-version=19.11\"";
  CompilerInfoData compiler_info;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::GetResourceDir(kDummyClangOutput,
                                                             &compiler_info));
  EXPECT_EQ(
      "c:\\\\third_party\\\\llvm-build\\\\"
      "Release+Asserts\\\\lib\\\\clang\\\\7.0.0",
      compiler_info.cxx().resource_dir());
}
#endif


TEST(ClangCompilerInfoBuilderHelperTest, ParseRealClangPathForChromeOS) {
  const char kClangVoutput[] =
      "Chromium OS 3.9_pre265926-r9 clang version 3.9.0 "
      "(/var/cache/chromeos-cache/distfiles/host/egit-src/clang.git "
      "af6a0b98569cf7981fe27327ac4bf19bd0d6b162) (/var/cache/chromeos"
      "-cache/distfiles/host/egit-src/llvm.git 26a9873b72c6dbb425ae07"
      "5fcf51caa9fc5e892b) (based on LLVM 3.9.0svn)\n"
      "Target: x86_64-cros-linux-gnu\n"
      "Thread model: posix\n"
      "InstalledDir: /usr/local/google/home/test/.cros_"
      "cache/chrome-sdk/tarballs/falco+8754.0.0+target_toolchain/usr/"
      "bin\n"
      "Found candidate GCC installation: /usr/local/google/home/test/"
      ".cros_cache/chrome-sdk/tarballs/falco+8754.0.0+target_toolchain/"
      "usr/bin/../lib/gcc/x86_64-cros-linux-gnu/4.9.x\n"
      "Selected GCC installation: /usr/local/google/home/test/.cros_cache"
      "/chrome-sdk/tarballs/falco+8754.0.0+target_toolchain/usr/bin/../"
      "lib/gcc/x86_64-cros-linux-gnu/4.9.x\n"
      "Candidate multilib: .;@m64\n"
      "Selected multilib: .;@m64\n"
      " \"/usr/local/google/home/test/usr/bin/clang-3.9\" -cc1 "
      "-triple x86_64-cros-linux-gnu -E -disable-free -disable-llvm-"
      "verifier -discard-value-names -main-file-name null "
      "-o - -x c /dev/null\n"
      "clang -cc1 version 3.9.0 based upon LLVM 3.9.0svn default target"
      " x86_64-pc-linux-gnu\n"
      "ignoring nonexistent directory \"/usr/local/google/test/"
      ".cros_cache/chrome-sdk/tarballs/falco+8754.0.0+sysroot_"
      "chromeos-base_chromeos-chrome.tar.xz/usr/local/include\"\n"
      "ignoring nonexistent directory \"/usr/local/google/home/test/"
      ".cros_cache/chrome-sdk/tarballs/falco+8754.0.0+sysroot_chromeos-"
      "base_chromeos-chrome.tar.xz/include\"\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " /usr/local/google/home/test/.cros_cache/chrome-sdk/tarballs/"
      "falco+8754.0.0+target_toolchain/usr/bin/../lib64/clang/3.9.0/"
      "include\n"
      " /usr/local/google/home/test/.cros_cache/chrome-sdk/tarballs/"
      "falco+8754.0.0+sysroot_chromeos-base_chromeos-chrome.tar.xz/"
      "usr/include\n"
      "End of search list.\n"
      "# 1 \"/dev/null\"\n"
      "# 1 \"<built-in>\" 1\n"
      "# 1 \"<built-in>\" 3\n"
      "# 321 \"<built-in>\" 3\n"
      "# 1 \"<command line>\" 1\n"
      "# 1 \"<built-in>\" 2\n"
      "# 1 \"/dev/null\" 2\n";

  const string path =
      ClangCompilerInfoBuilderHelper::ParseRealClangPath(kClangVoutput);
  EXPECT_EQ("/usr/local/google/home/test/usr/bin/clang-3.9", path);
}

TEST(ClangCompilerInfoBuilderHelperTest, ParseClangVersionTarget) {
  static const char kClangSharpOutput[] =
      "clang version 3.5 (trunk)\n"
      "Target: i686-pc-win32\n"
      "Thread model: posix\n";
  string version, target;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::ParseClangVersionTarget(
      kClangSharpOutput, &version, &target));
  EXPECT_EQ("clang version 3.5 (trunk)", version);
  EXPECT_EQ("i686-pc-win32", target);
}

TEST(ClangCompilerInfoBuilderHelperTest, ParseClangVersionTargetCRLF) {
  static const char kClangSharpOutput[] =
      "clang version 7.0.0 (trunk 324578)\r\n"
      "Target: x86_64-pc-windows-msvc\r\n"
      "Thread model: posix\r\n"
      "InstalledDIr: C:\\somewhere\\\r\n";
  string version, target;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::ParseClangVersionTarget(
      kClangSharpOutput, &version, &target));
  EXPECT_EQ("clang version 7.0.0 (trunk 324578)", version);
  EXPECT_EQ("x86_64-pc-windows-msvc", target);
}

TEST(ClangCompilerInfoBuilderHelperTest, ParseFeatures) {
  static const char* kDummyObjectMacros[] = {
      "dummy_macro1", "dummy_macro2",
  };
  static const unsigned long kNumDummyObjectMacros =
      ABSL_ARRAYSIZE(kDummyObjectMacros);
  static const char* kDummyFunctionMacros[] = {
      "dummy_func1", "dummy_func2",
  };
  static const unsigned long kNumDummyFunctionMacros =
      ABSL_ARRAYSIZE(kDummyFunctionMacros);
  static const char* kDummyFeatures[] = {
      "dummy_feature1", "dummy_feature2",
  };
  static const unsigned long kNumDummyFeatures = ABSL_ARRAYSIZE(kDummyFeatures);
  static const char* kDummyExtensions[] = {
      "dummy_extension1", "dummy_extension2",
  };
  static const unsigned long kNumDummyExtensions =
      ABSL_ARRAYSIZE(kDummyExtensions);
  static const char* kDummyAttributes[] = {
      "dummy_attribute1", "dummy_attribute2", "dummy_attribute3",
      "dummy_attribute4", "_Alignas",         "asm",
  };
  static const unsigned long kNumDummyAttributes =
      ABSL_ARRAYSIZE(kDummyAttributes);
  static const char* kDummyCppAttributes[] = {
      "dummy_cpp_attribute1", "dummy_cpp_attribute2",
      "clang::dummy_cpp_attribute1", "clang::dummy_cpp_attribute2",
  };
  static const unsigned long kNumDummyCppAttributes =
      ABSL_ARRAYSIZE(kDummyCppAttributes);

  static const char* kDummyDeclspecAttributes[] = {
      "dummy_declspec_attributes1", "dummy_declspec_attributes2",
  };
  static const unsigned long kNumDummyDeclspecAttributes =
      ABSL_ARRAYSIZE(kDummyDeclspecAttributes);

  static const char* kDummyBuiltins[] = {
      "dummy_builtin1", "dummy_builtin2",
  };
  static const unsigned long kNumDummyBuiltins = ABSL_ARRAYSIZE(kDummyBuiltins);

  static const char kClangOutput[] =
      "# 1 \"a.c\"\n"
      "# 1 \"a.c\" 1\n"
      "# 1 \"<built-in>\" 1\n"
      "# 1 \"<built-in>\" 3\n"
      "# 132 \"<built-in>\" 3\n"
      "# 1 \"<command line>\" 1\n"
      "# 1 \"<built-in>\" 2\n"
      "# 1 \"a.c\" 2\n"
      "# 1 \"a.c\"\n"  // object macros.
      "1\n"
      "# 2 \"a.c\"\n"
      "0\n"
      "# 3 \"a.c\"\n"  // function macros.
      "1\n"
      "# 4 \"a.c\"\n"
      "0\n"
      "# 5 \"a.c\"\n"  // features.
      "1\n"
      "# 6 \"a.c\"\n"
      "0\n"
      "# 7 \"a.c\"\n"  // extensions.
      "1\n"
      "# 8 \"a.c\"\n"
      "0\n"
      "# 9 \"a.c\"\n"  // attributes.
      "1\n"
      "# 10 \"a.c\"\n"
      "0)\n"
      "# 11 \"a.c\"\n"
      "1\n"
      "# 12\n"
      "0\n"
      "# 13\n"
      "_Alignas)\n"
      "# 14\n"
      "asm)\n"
      "# 15\n"  // cpp attributes.
      "201304\n"
      "# 16\n"
      "0\n"
      "# 17\n"
      "201301\n"
      "# 18\n"
      "0\n"
      "# 19\n"  // declspec attributes.
      "1\n"
      "# 20\n"
      "0\n"
      "# 21\n"  // builtins
      "1\n"
      "# 22\n"
      "0\n";

  ClangCompilerInfoBuilderHelper::FeatureList object_macros =
      std::make_pair(kDummyObjectMacros, kNumDummyObjectMacros);
  ClangCompilerInfoBuilderHelper::FeatureList function_macros =
      std::make_pair(kDummyFunctionMacros, kNumDummyFunctionMacros);
  ClangCompilerInfoBuilderHelper::FeatureList features =
      std::make_pair(kDummyFeatures, kNumDummyFeatures);
  ClangCompilerInfoBuilderHelper::FeatureList extensions =
      std::make_pair(kDummyExtensions, kNumDummyExtensions);
  ClangCompilerInfoBuilderHelper::FeatureList attributes =
      std::make_pair(kDummyAttributes, kNumDummyAttributes);
  ClangCompilerInfoBuilderHelper::FeatureList cpp_attributes =
      std::make_pair(kDummyCppAttributes, kNumDummyCppAttributes);
  ClangCompilerInfoBuilderHelper::FeatureList declspec_attributes =
      std::make_pair(kDummyDeclspecAttributes, kNumDummyDeclspecAttributes);
  ClangCompilerInfoBuilderHelper::FeatureList builtins =
      std::make_pair(kDummyBuiltins, kNumDummyBuiltins);

  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::ParseFeatures(
      kClangOutput, object_macros, function_macros, features, extensions,
      attributes, cpp_attributes, declspec_attributes, builtins, cid.get()));

  CxxCompilerInfo info(std::move(cid));

  EXPECT_EQ(2U, info.supported_predefined_macros().size());
  EXPECT_EQ(1U, info.supported_predefined_macros().count("dummy_macro1"));
  EXPECT_EQ(0U, info.supported_predefined_macros().count("dummy_macro2"));
  EXPECT_EQ(1U, info.supported_predefined_macros().count("dummy_func1"));
  EXPECT_EQ(0U, info.supported_predefined_macros().count("dummy_func2"));

  EXPECT_EQ(1U, info.has_feature().size());
  EXPECT_EQ(1, FindValue(info.has_feature(), "dummy_feature1"));
  EXPECT_EQ(0U, info.has_feature().count("dummy_feature2"));

  EXPECT_EQ(1U, info.has_extension().size());
  EXPECT_EQ(1, FindValue(info.has_extension(), "dummy_extension1"));
  EXPECT_EQ(0U, info.has_extension().count("dummy_extension2"));

  EXPECT_EQ(2U, info.has_attribute().size());
  EXPECT_EQ(1, FindValue(info.has_attribute(), "dummy_attribute1"));
  EXPECT_EQ(0U, info.has_attribute().count("dummy_attribute2"));
  EXPECT_EQ(1, FindValue(info.has_attribute(), "dummy_attribute3"));
  EXPECT_EQ(0U, info.has_attribute().count("dummy_attribute4"));
  EXPECT_EQ(0U, info.has_attribute().count("_Alignas"));
  EXPECT_EQ(0U, info.has_attribute().count("asm"));

  EXPECT_EQ(2U, info.has_cpp_attribute().size());
  EXPECT_EQ(201304,
            FindValue(info.has_cpp_attribute(), "dummy_cpp_attribute1"));
  EXPECT_EQ(0U, info.has_cpp_attribute().count("dummy_cpp_attribute2"));
  EXPECT_EQ(201301,
            FindValue(info.has_cpp_attribute(), "clang::dummy_cpp_attribute1"));
  EXPECT_EQ(0U, info.has_cpp_attribute().count("clang::dummy_cpp_attribute2"));

  EXPECT_EQ(1U, info.has_declspec_attribute().size());
  EXPECT_EQ(1, FindValue(info.has_declspec_attribute(),
                         "dummy_declspec_attributes1"));
  EXPECT_EQ(0U,
            info.has_declspec_attribute().count("dummy_declspec_attributes2"));

  EXPECT_EQ(1, FindValue(info.has_builtin(), "dummy_builtin1"));
  EXPECT_EQ(0U, info.has_builtin().count("dummy_builtin2"));

  // check `#line <number> "<filename>"` format.
  static const char kClangClOutput[] =
      "#line 1 \"a.c\"\n"
      "#line 1 \"a.c\" 1\n"
      "#line 1 \"<built-in>\" 1\n"
      "#line 1 \"<built-in>\" 3\n"
      "#line 132 \"<built-in>\" 3\n"
      "#line 1 \"<command line>\" 1\n"
      "#line 1 \"<built-in>\" 2\n"
      "#line 1 \"a.c\" 2\n"
      "#line 1 \"a.c\"\n"  // object macros.
      "1\n"
      "#line 2 \"a.c\"\n"
      "0\n"
      "#line 3 \"a.c\"\n"  // function macros.
      "1\n"
      "#line 4 \"a.c\"\n"
      "0\n"
      "#line 5 \"a.c\"\n"  // features.
      "1\n"
      "#line 6 \"a.c\"\n"
      "0\n"
      "#line 7 \"a.c\"\n"  // extensions.
      "1\n"
      "#line 8 \"a.c\"\n"
      "0\n"
      "#line 9 \"a.c\"\n"  // attributes.
      "1\n"
      "#line 10 \"a.c\"\n"
      "0)\n"
      "#line 11 \"a.c\"\n"
      "1\n"
      "#line 12\n"
      "0\n"
      "#line 13\n"
      "_Alignas)\n"
      "#line 14\n"
      "asm)\n"
      "#line 15\n"  // cpp attributes.
      "201304\n"
      "#line 16\n"
      "0\n"
      "#line 17\n"
      "201301\n"
      "#line 18\n"
      "0\n"
      "#line 19\n"  // declspec attributes.
      "1\n"
      "#line 20\n"
      "0\n"
      "#line 21\n"  // builtins
      "1\n"
      "#line 22\n"
      "0\n";

  std::unique_ptr<CompilerInfoData> cid_cl(new CompilerInfoData);
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::ParseFeatures(
      kClangClOutput, object_macros, function_macros, features, extensions,
      attributes, cpp_attributes, declspec_attributes, builtins, cid_cl.get()));
  CxxCompilerInfo info_cl(std::move(cid_cl));

  EXPECT_EQ(2U, info_cl.supported_predefined_macros().size());
  EXPECT_EQ(1U, info_cl.supported_predefined_macros().count("dummy_macro1"));
  EXPECT_EQ(0U, info_cl.supported_predefined_macros().count("dummy_macro2"));
  EXPECT_EQ(1U, info_cl.supported_predefined_macros().count("dummy_func1"));
  EXPECT_EQ(0U, info_cl.supported_predefined_macros().count("dummy_func2"));

  EXPECT_EQ(1U, info_cl.has_feature().size());
  EXPECT_EQ(1, FindValue(info_cl.has_feature(), "dummy_feature1"));
  EXPECT_EQ(0U, info_cl.has_feature().count("dummy_feature2"));

  EXPECT_EQ(1U, info_cl.has_extension().size());
  EXPECT_EQ(1, FindValue(info_cl.has_extension(), "dummy_extension1"));
  EXPECT_EQ(0U, info_cl.has_extension().count("dummy_extension2"));

  EXPECT_EQ(2U, info_cl.has_attribute().size());
  EXPECT_EQ(1, FindValue(info_cl.has_attribute(), "dummy_attribute1"));
  EXPECT_EQ(0U, info_cl.has_attribute().count("dummy_attribute2"));
  EXPECT_EQ(1, FindValue(info_cl.has_attribute(), "dummy_attribute3"));
  EXPECT_EQ(0U, info_cl.has_attribute().count("dummy_attribute4"));
  EXPECT_EQ(0U, info_cl.has_attribute().count("_Alignas"));
  EXPECT_EQ(0U, info_cl.has_attribute().count("asm"));

  EXPECT_EQ(2U, info_cl.has_cpp_attribute().size());
  EXPECT_EQ(201304,
            FindValue(info_cl.has_cpp_attribute(), "dummy_cpp_attribute1"));
  EXPECT_EQ(0U, info_cl.has_cpp_attribute().count("dummy_cpp_attribute2"));
  EXPECT_EQ(201301, FindValue(info_cl.has_cpp_attribute(),
                              "clang::dummy_cpp_attribute1"));
  EXPECT_EQ(0U,
            info_cl.has_cpp_attribute().count("clang::dummy_cpp_attribute2"));

  EXPECT_EQ(1U, info_cl.has_declspec_attribute().size());
  EXPECT_EQ(1, FindValue(info_cl.has_declspec_attribute(),
                         "dummy_declspec_attributes1"));
  EXPECT_EQ(
      0U, info_cl.has_declspec_attribute().count("dummy_declspec_attributes2"));

  EXPECT_EQ(1, FindValue(info_cl.has_builtin(), "dummy_builtin1"));
  EXPECT_EQ(0U, info_cl.has_builtin().count("dummy_builtin2"));
}

#ifdef _WIN32
TEST(ClangCompilerInfoBuilderHelperTest, SplitGccIncludeOutputForClang) {
  static const char kClangOutput[] =
      "clang -cc1 version 3.5 based upon LLVM 3.5svn default target "
      "i686-pc-win32\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " C:\\Users\\goma\\proj\\clang\\trying\\build\\bin\\..\\lib"
      "\\clang\\3.5\\include\n"
      " C:\\Program Files (x86)\\Microsoft Visual Studio 11.0\\VC\\INCLUDE\n"
      " C:\\Program Files (x86)\\Microsoft Visual Studio 11.0\\VC\\ATLMFC"
      "\\INCLUDE\n"
      " C:\\Program Files (x86)\\Windows Kits\\8.0\\include\\shared\n"
      " C:\\Program Files (x86)\\Windows Kits\\8.0\\include\\um\n"
      " C:\\Program Files (x86)\\Windows Kits\\8.0\\include\\winrt\n"
      "End of search list.\n"
      "#line 1 \"..\\\\..\\\\proj\\\\clang\\\\empty.cc\"\n"
      "#line 1 \"<built-in>\"\n"
      "#line 1 \"<built-in>\"\n"
      "#line 176 \"<built-in>\"\n"
      "#line 1 \"<command line>\"\n"
      "#line 1 \"<built-in>\"\n"
      "#line 1 \"..\\\\..\\\\proj\\\\clang\\\\empty.cc\"\n";

  std::vector<string> qpaths;
  std::vector<string> paths;
  std::vector<string> framework_paths;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::SplitGccIncludeOutput(
      kClangOutput, &qpaths, &paths, &framework_paths));

  EXPECT_TRUE(qpaths.empty());
  std::vector<string> expected_paths;
  expected_paths.push_back(
      "C:\\Users\\goma\\proj\\clang\\trying\\build\\bin\\..\\lib"
      "\\clang\\3.5\\include");
  expected_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft Visual Studio 11.0\\VC\\INCLUDE");
  expected_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft Visual Studio 11.0\\VC\\ATLMFC"
      "\\INCLUDE");
  expected_paths.push_back(
      "C:\\Program Files (x86)\\Windows Kits\\8.0\\include\\shared");
  expected_paths.push_back(
      "C:\\Program Files (x86)\\Windows Kits\\8.0\\include\\um");
  expected_paths.push_back(
      "C:\\Program Files (x86)\\Windows Kits\\8.0\\include\\winrt");
  EXPECT_EQ(expected_paths, paths);
  EXPECT_TRUE(framework_paths.empty());
}
#endif

TEST(ClangCompilerInfoBuilderHelperTest, SplitGccIncludeOutputForIQuote) {
  // gtrusty gcc-4.8 -xc++ -iquote include -v -E /dev/null -o /dev/null
  static const char kGccVOutput[] =
      "Using built-in specs.\n"
      "COLLECT_GCC=gcc\n"
      "Target: x86_64-linux-gnu\n"
      "Configured with: ../src/configure -v "
      "--with-pkgversion='Ubuntu 4.8.4-2ubuntu1~14.04.3' "
      "--with-bugurl=file:///usr/share/doc/gcc-4.8/README.Bugs "
      "--enable-languages=c,c++,java,go,d,fortran,objc,obj-c++ "
      "--prefix=/usr --program-suffix=-4.8 --enable-shared "
      "--enable-linker-build-id --libexecdir=/usr/lib "
      "--without-included-gettext --enable-threads=posix "
      "--with-gxx-include-dir=/usr/include/c++/4.8 --libdir=/usr/lib "
      "--enable-nls --with-sysroot=/ --enable-clocale=gnu "
      "--enable-libstdcxx-debug --enable-libstdcxx-time=yes "
      "--enable-gnu-unique-object --disable-libmudflap --enable-plugin "
      "--with-system-zlib --disable-browser-plugin --enable-java-awt=gtk "
      "--enable-gtk-cairo "
      "--with-java-home=/usr/lib/jvm/java-1.5.0-gcj-4.8-amd64/jre "
      "--enable-java-home "
      "--with-jvm-root-dir=/usr/lib/jvm/java-1.5.0-gcj-4.8-amd64 "
      "--with-jvm-jar-dir=/usr/lib/jvm-exports/java-1.5.0-gcj-4.8-amd64 "
      "--with-arch-directory=amd64 "
      "--with-ecj-jar=/usr/share/java/eclipse-ecj.jar "
      "--enable-objc-gc --enable-multiarch --disable-werror "
      "--with-arch-32=i686 --with-abi=m64 --with-multilib-list=m32,m64,mx32 "
      "--with-tune=generic --enable-checking=release "
      "--build=x86_64-linux-gnu --host=x86_64-linux-gnu "
      "--target=x86_64-linux-gnu\n"
      "Thread model: posix\n"
      "gcc version 4.8.4 (Ubuntu 4.8.4-2ubuntu1~14.04.3) \n"
      "COLLECT_GCC_OPTIONS='-v' '-iquote' 'include' '-E' '-mtune=generic' "
      "'-march=x86-64'\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.8/cc1plus -E -quiet -v "
      "-imultiarch x86_64-linux-gnu -D_GNU_SOURCE -iquote include /dev/null "
      "-quiet -dumpbase null -mtune=generic -march=x86-64 -auxbase null "
      "-version -fstack-protector -Wformat -Wformat-security\n"
      "ignoring duplicate directory "
      "\"/usr/include/x86_64-linux-gnu/c++/4.8\"\n"
      "ignoring nonexistent directory "
      "\"/usr/local/include/x86_64-linux-gnu\"\n"
      "ignoring nonexistent directory "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../../"
      "x86_64-linux-gnu/include\"\n"
      "#include \"...\" search starts here:\n"
      " include\n"
      "#include <...> search starts here:\n"
      " /usr/include/c++/4.8\n"
      " /usr/include/x86_64-linux-gnu/c++/4.8\n"
      " /usr/include/c++/4.8/backward\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.8/include\n"
      " /usr/local/include\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.8/include-fixed\n"
      " /usr/include/x86_64-linux-gnu\n"
      " /usr/include\n"
      "End of search list.\n"
      "COMPILER_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.8/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.8/:/usr/lib/gcc/x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.8/:/usr/lib/gcc/x86_64-linux-gnu/\n"
      "LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.8/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../../lib/:"
      "/lib/x86_64-linux-gnu/:/lib/../lib/:/usr/lib/x86_64-linux-gnu/:"
      "/usr/lib/../lib/:/usr/lib/gcc/x86_64-linux-gnu/4.8/../../../:/lib/:"
      "/usr/lib/\n"
      "COLLECT_GCC_OPTIONS='-v' '-iquote' 'include' '-E' '-mtune=generic' "
      "'-march=x86-64'\n";

  std::vector<string> qpaths;
  std::vector<string> paths;
  std::vector<string> framework_paths;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::SplitGccIncludeOutput(
      kGccVOutput, &qpaths, &paths, &framework_paths));

  const std::vector<string> expected_qpaths{
      "include",
  };
  EXPECT_EQ(expected_qpaths, qpaths);
  const std::vector<string> expected_paths{
      "/usr/include/c++/4.8",
      "/usr/include/x86_64-linux-gnu/c++/4.8",
      "/usr/include/c++/4.8/backward",
      "/usr/lib/gcc/x86_64-linux-gnu/4.8/include",
      "/usr/local/include",
      "/usr/lib/gcc/x86_64-linux-gnu/4.8/include-fixed",
      "/usr/include/x86_64-linux-gnu",
      "/usr/include",
  };
  EXPECT_EQ(expected_paths, paths);
  EXPECT_TRUE(framework_paths.empty());
}

TEST(ClangCompilerInfoBuilderHelperTest, SplitGccIncludeOutput) {
  // glucid gcc-4.4.3
  static const char kGccVOutput[] =
      "Using built-in specs.\n"
      "Target: x86_64-linux-gnu\n"
      "Configured with: ../src/configure -v "
      "--with-pkgversion='Ubuntu 4.4.3-4ubuntu5.1' "
      "--with-bugurl=file:///usr/share/doc/gcc-4.4/README.Bugs "
      "--enable-languages=c,c++,fortran,objc,obj-c++ "
      "--prefix=/usr --enable-shared --enable-multiarch "
      "--enable-linker-build-id --with-system-zlib --libexecdir=/usr/lib "
      "--without-included-gettext --enable-threads=posix "
      "--with-gxx-include-dir=/usr/include/c++/4.4 --program-suffix=-4.4 "
      "--enable-nls --enable-clocale=gnu --enable-libstdcxx-debug "
      "--enable-plugin --enable-objc-gc --disable-werror --with-arch-32=i486 "
      "--with-tune=generic --enable-checking=release --build=x86_64-linux-gnu "
      "--host=x86_64-linux-gnu --target=x86_64-linux-gnu\n"
      "Thread model: posix\n"
      "gcc version 4.4.3 (Ubuntu 4.4.3-4ubuntu5.1) \n"
      "COLLECT_GCC_OPTIONS='-v' '-E' '-o' '/dev/null' '-shared-libgcc' "
      "'-mtune=generic'\n"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/cc1 -E -quiet -v /dev/null "
      "-D_FORTIFY_SOURCE=2 -o /dev/null -mtune=generic -fstack-protector\n"
      "ignoring nonexistent directory \"/usr/local/include/x86_64-linux-gnu\"\n"
      "ignoring nonexistent directory \"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/"
      "../../../../x86_64-linux-gnu/include\"\n"
      "ignoring nonexistent directory \"/usr/include/x86_64-linux-gnu\"\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " /usr/local/include\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.4.3/include\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.4.3/include-fixed\n"
      " /usr/include\n"
      "End of search list.\n"
      "COMPILER_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu/\n"
      "LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/:/lib/../lib/:"
      "/usr/lib/../lib/:/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../:/lib/:"
      "/usr/lib/:/usr/lib/x86_64-linux-gnu/\n"
      "COLLECT_GCC_OPTIONS='-v' '-E' '-o' '/dev/null' '-shared-libgcc' "
      "'-mtune=generic'\n";

  std::vector<string> qpaths;
  std::vector<string> paths;
  std::vector<string> framework_paths;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::SplitGccIncludeOutput(
      kGccVOutput, &qpaths, &paths, &framework_paths));

  EXPECT_TRUE(qpaths.empty());
  std::vector<string> expected_paths;
  expected_paths.push_back("/usr/local/include");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include-fixed");
  expected_paths.push_back("/usr/include");
  EXPECT_EQ(expected_paths, paths);
  EXPECT_TRUE(framework_paths.empty());
}

TEST(ClangCompilerInfoBuilderHelperTest,
     SplitGccIncludeOutputWithCurIncludePath) {
  // glucid gcc-4.4.3 with C_INCLUDE_PATH=.
  static const char kGccVOutput[] =
      "Using built-in specs.\n"
      "Target: x86_64-linux-gnu\n"
      "Configured with: ../src/configure -v "
      "--with-pkgversion='Ubuntu 4.4.3-4ubuntu5.1' "
      "--with-bugurl=file:///usr/share/doc/gcc-4.4/README.Bugs "
      "--enable-languages=c,c++,fortran,objc,obj-c++ "
      "--prefix=/usr --enable-shared --enable-multiarch "
      "--enable-linker-build-id --with-system-zlib --libexecdir=/usr/lib "
      "--without-included-gettext --enable-threads=posix "
      "--with-gxx-include-dir=/usr/include/c++/4.4 --program-suffix=-4.4 "
      "--enable-nls --enable-clocale=gnu --enable-libstdcxx-debug "
      "--enable-plugin --enable-objc-gc --disable-werror --with-arch-32=i486 "
      "--with-tune=generic --enable-checking=release --build=x86_64-linux-gnu "
      "--host=x86_64-linux-gnu --target=x86_64-linux-gnu\n"
      "Thread model: posix\n"
      "gcc version 4.4.3 (Ubuntu 4.4.3-4ubuntu5.1) \n"
      "COLLECT_GCC_OPTIONS='-v' '-E' '-o' '/dev/null' '-shared-libgcc' "
      "'-mtune=generic'\n"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/cc1 -E -quiet -v /dev/null "
      "-D_FORTIFY_SOURCE=2 -o /dev/null -mtune=generic -fstack-protector\n"
      "ignoring nonexistent directory \"/usr/local/include/x86_64-linux-gnu\"\n"
      "ignoring nonexistent directory \"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/"
      "../../../../x86_64-linux-gnu/include\"\n"
      "ignoring nonexistent directory \"/usr/include/x86_64-linux-gnu\"\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " .\n"
      " /usr/local/include\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.4.3/include\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.4.3/include-fixed\n"
      " /usr/include\n"
      "End of search list.\n"
      "COMPILER_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu/\n"
      "LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/:/lib/../lib/:"
      "/usr/lib/../lib/:/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../:/lib/:"
      "/usr/lib/:/usr/lib/x86_64-linux-gnu/\n"
      "COLLECT_GCC_OPTIONS='-v' '-E' '-o' '/dev/null' '-shared-libgcc' "
      "'-mtune=generic'\n";

  std::vector<string> qpaths;
  std::vector<string> paths;
  std::vector<string> framework_paths;
  EXPECT_TRUE(ClangCompilerInfoBuilderHelper::SplitGccIncludeOutput(
      kGccVOutput, &qpaths, &paths, &framework_paths));

  EXPECT_TRUE(qpaths.empty());
  std::vector<string> expected_paths;
  expected_paths.push_back(".");
  expected_paths.push_back("/usr/local/include");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include-fixed");
  expected_paths.push_back("/usr/include");
  EXPECT_EQ(expected_paths, paths);
  EXPECT_TRUE(framework_paths.empty());
}


}  // namespace devtools_goma
