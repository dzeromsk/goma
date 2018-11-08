// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gcc_flags.h"

#include <limits.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "base/filesystem.h"
#include "base/options.h"
#include "base/path.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"
#include "lib/compiler_flags_parser.h"
#include "lib/file_helper.h"
#include "lib/known_warning_options.h"
#include "lib/path_resolver.h"

#ifdef _WIN32
#include "config_win.h"
// we'll ignore the warnings:
// warning C4996: 'strdup': The POSIX name for this item is deprecated.
#pragma warning(disable : 4996)
#endif  // _WIN32

using google::GetExistingTempDirectories;
using std::string;
using absl::StrCat;

namespace devtools_goma {

static void ExpectHasElement(const std::vector<string>& v,
                             const string& elem) {
  EXPECT_TRUE(std::find(v.begin(), v.end(), elem) != v.end()) << elem;
}

class GCCFlagsTest : public testing::Test {
 protected:
  void SetUp() override {
    std::vector<string> tmp_dirs;
    GetExistingTempDirectories(&tmp_dirs);
    CHECK_GT(tmp_dirs.size(), 0);

#ifndef _WIN32
    string pid = std::to_string(getpid());
#else
    string pid = std::to_string(GetCurrentProcessId());
#endif
    tmp_dir_ =
        file::JoinPath(tmp_dirs[0], StrCat("compiler_flags_unittest_", pid));

    ASSERT_TRUE(file::CreateDir(tmp_dir_, file::CreationMode(0777)).ok());
  }
  void TearDown() override {
    util::Status status = file::RecursivelyDelete(tmp_dir_, file::Defaults());
    if (!status.ok()) {
      LOG(ERROR) << "delete " << tmp_dir_;
    }
  }
  string GetLanguage(const string& compiler_name,
                     const string& input_filename) {
    return GCCFlags::GetLanguage(compiler_name, input_filename);
  }

  string tmp_dir_;
};

TEST_F(GCCFlagsTest, GetLanguage) {
  EXPECT_EQ("c", GetLanguage("gcc", "foo"));
  EXPECT_EQ("c", GetLanguage("gcc", "foo.c"));
  EXPECT_EQ("c++", GetLanguage("gcc", "foo.cc"));
  EXPECT_EQ("c++", GetLanguage("gcc", "foo.cpp"));
  EXPECT_EQ("c++", GetLanguage("g++", "foo"));
  EXPECT_EQ("c++", GetLanguage("g++", "foo.c"));
  EXPECT_EQ("c++", GetLanguage("g++", "foo.cc"));
  EXPECT_EQ("c++", GetLanguage("g++", "foo.cpp"));
  EXPECT_EQ("objective-c", GetLanguage("gcc", "foo.m"));
  EXPECT_EQ("objective-c", GetLanguage("g++", "foo.m"));
  EXPECT_EQ("objective-c++", GetLanguage("gcc", "foo.mm"));
  EXPECT_EQ("objective-c++", GetLanguage("g++", "foo.mm"));
  EXPECT_EQ("c-header", GetLanguage("gcc", "foo.h"));
  EXPECT_EQ("c++-header", GetLanguage("gcc", "foo.hpp"));
  EXPECT_EQ("c++-header", GetLanguage("g++", "foo.h"));

  // clang rule.
  EXPECT_EQ("c", GetLanguage("clang", "foo"));
  EXPECT_EQ("c", GetLanguage("clang", "foo.c"));
  EXPECT_EQ("c++", GetLanguage("clang", "foo.cc"));
  EXPECT_EQ("c++", GetLanguage("clang", "foo.cpp"));
  EXPECT_EQ("c++", GetLanguage("clang++", "foo"));
  EXPECT_EQ("c++", GetLanguage("clang++", "foo.c"));
  EXPECT_EQ("c++", GetLanguage("clang++", "foo.cc"));
  EXPECT_EQ("c++", GetLanguage("clang++", "foo.cpp"));
  EXPECT_EQ("objective-c", GetLanguage("clang", "foo.m"));
  EXPECT_EQ("objective-c", GetLanguage("clang++", "foo.m"));
  EXPECT_EQ("objective-c++", GetLanguage("clang", "foo.mm"));
  EXPECT_EQ("objective-c++", GetLanguage("clang++", "foo.mm"));
  EXPECT_EQ("c-header", GetLanguage("clang", "foo.h"));
  EXPECT_EQ("c++-header", GetLanguage("clang", "foo.hpp"));
  EXPECT_EQ("c++-header", GetLanguage("clang++", "foo.h"));
}

TEST_F(GCCFlagsTest, Basic) {
  std::vector<string> args;
  args.push_back("/usr/bin/x86_64-pc-linux-gnu-gcc-4.3");
  args.push_back("-c");
  args.push_back("-m32");
  args.push_back("-mtune=generic");
  args.push_back("foobar.c");
  args.push_back("-oout/foobar.o");
  args.push_back("-MF");
  args.push_back("deps/foobar.d");
  args.push_back("-Wp,-MD,deps/foobar2.d");
  args.push_back("-L");
  args.push_back("/usr/local/lib");
  args.push_back("-I");
  args.push_back("/usr/local/include");
  args.push_back("-D");
  args.push_back("FOO");
  args.push_back("-Uhoge");
  args.push_back("-isystem");
  args.push_back("/usr");
  args.push_back("-include");
  args.push_back("/usr/include/stdio.h");
  args.push_back("-imacros");
  args.push_back("/usr/include/stdlib.h");
  args.push_back("--include");
  args.push_back("/usr/include/string.h");
  args.push_back("--imacros");
  args.push_back("/usr/include/stdint.h");
  args.push_back("-MT");
  args.push_back("hoge");
  args.push_back("-isysroot");
  args.push_back("/tmp");
  args.push_back("-x");
  args.push_back("c++");
  args.push_back("-arch");
  args.push_back("ppc");
  args.push_back("-g");
  args.push_back("-nostdinc");
  args.push_back("-nostdinc++");
  args.push_back("-nostdlibinc");
  args.push_back("--param");
  args.push_back("key=value");
  args.push_back("-b");
  args.push_back("i386");
  args.push_back("-V");
  args.push_back("4.0");
  args.push_back("-specs");
  args.push_back("foo.spec");
  args.push_back("-std");
  args.push_back("c99");
  args.push_back("-target");
  args.push_back("arm-linux-androideabi");

  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("x86_64-pc-linux-gnu-gcc-4.3", flags.compiler_base_name());
  EXPECT_EQ("gcc", flags.compiler_name());

  const std::vector<string> expected_compiler_info_flags{
      "-m32",
      // TODO: This doesn't change include directory actually.
      "-mtune=generic", "-isystem", "/usr", "-arch", "ppc", "-nostdinc++",
      "-nostdlibinc", "-b", "i386", "-V", "4.0", "-specs", "foo.spec", "-std",
      "c99", "-target", "arm-linux-androideabi", "-x", "c++", "-nostdinc",
      "-isysroot", "/tmp",
  };
  EXPECT_EQ(expected_compiler_info_flags, flags.compiler_info_flags());

  ASSERT_EQ(1U, flags.input_filenames().size());
  EXPECT_EQ("foobar.c", flags.input_filenames()[0]);

  ASSERT_EQ(1U, flags.include_dirs().size());
  EXPECT_EQ("/usr/local/include", flags.include_dirs()[0]);

  EXPECT_EQ(1U, flags.non_system_include_dirs().size());
  EXPECT_EQ("/usr/local/include", flags.include_dirs()[0]);

  EXPECT_EQ(4U, flags.root_includes().size());
  EXPECT_EQ("/usr/include/stdlib.h", flags.root_includes()[0]);
  EXPECT_EQ("/usr/include/stdint.h", flags.root_includes()[1]);
  EXPECT_EQ("/usr/include/stdio.h", flags.root_includes()[2]);
  EXPECT_EQ("/usr/include/string.h", flags.root_includes()[3]);

  EXPECT_EQ(0U, flags.framework_dirs().size());
  EXPECT_EQ(2U, flags.commandline_macros().size());
  EXPECT_EQ("FOO", flags.commandline_macros()[0].first);
  EXPECT_TRUE(flags.commandline_macros()[0].second);
  EXPECT_EQ("hoge", flags.commandline_macros()[1].first);
  EXPECT_FALSE(flags.commandline_macros()[1].second);

  // output file order is not important.
  const std::set<string> expected_output_files{"out/foobar.o", "deps/foobar.d",
                                               "deps/foobar2.d"};
  EXPECT_EQ(expected_output_files,
            std::set<string>(flags.output_files().begin(),
                             flags.output_files().end()));

  EXPECT_TRUE(flags.is_cplusplus());
  EXPECT_TRUE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ("/tmp", flags.isysroot());
  EXPECT_EQ(CompilerFlagType::Gcc, flags.type());
}

TEST_F(GCCFlagsTest, Optimize) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-O");
  args.push_back("-o");
  args.push_back("hello.o");
  args.push_back("-c");
  args.push_back("hello.c");

  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("gcc", flags.compiler_base_name());
  EXPECT_EQ("gcc", flags.compiler_name());

  ASSERT_EQ(1, static_cast<int>(flags.compiler_info_flags().size()));
  EXPECT_EQ("-O", flags.compiler_info_flags()[0]);

  ASSERT_EQ(1, static_cast<int>(flags.input_filenames().size()));
  EXPECT_EQ("hello.c", flags.input_filenames()[0]);

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(1, static_cast<int>(output_files.size()));
  EXPECT_EQ("hello.o", output_files[0]);

  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ(CompilerFlagType::Gcc, flags.type());
}

TEST_F(GCCFlagsTest, GxxBaseName) {
  std::vector<string> args;
  args.push_back("/usr/bin/x86_64-pc-linux-gnu-g++-4.3");
  GCCFlags flags(args, "/");
  EXPECT_EQ("x86_64-pc-linux-gnu-g++-4.3", flags.compiler_base_name());
  EXPECT_EQ("g++", flags.compiler_name());
  EXPECT_TRUE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
}

TEST_F(GCCFlagsTest, Fission) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-gsplit-dwarf");
  args.push_back("-o");
  args.push_back("hello.o");
  args.push_back("-c");
  args.push_back("hello.c");

  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("gcc", flags.compiler_base_name());
  EXPECT_EQ("gcc", flags.compiler_name());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(2U, output_files.size());
  EXPECT_EQ("hello.o", output_files[0]);
  EXPECT_EQ("hello.dwo", output_files[1]);

  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ(CompilerFlagType::Gcc, flags.type());
}

TEST_F(GCCFlagsTest, FissionNoO) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-gsplit-dwarf");
  args.push_back("-c");
  args.push_back("hello.c");

  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("gcc", flags.compiler_base_name());
  EXPECT_EQ("gcc", flags.compiler_name());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(2U, output_files.size());
  EXPECT_EQ("hello.o", output_files[0]);
  EXPECT_EQ("hello.dwo", output_files[1]);

  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ(CompilerFlagType::Gcc, flags.type());
}

TEST_F(GCCFlagsTest, FissionDifferentOutput) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-gsplit-dwarf");
  args.push_back("-o");
  args.push_back("world.o");
  args.push_back("-c");
  args.push_back("hello.c");

  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("gcc", flags.compiler_base_name());
  EXPECT_EQ("gcc", flags.compiler_name());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(2U, output_files.size());
  EXPECT_EQ("world.o", output_files[0]);
  EXPECT_EQ("world.dwo", output_files[1]);

  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ(CompilerFlagType::Gcc, flags.type());
}

TEST_F(GCCFlagsTest, FissionCompileAndLink) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-gsplit-dwarf");
  args.push_back("-o");
  args.push_back("world");
  args.push_back("hello.c");

  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::LINK, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("gcc", flags.compiler_base_name());
  EXPECT_EQ("gcc", flags.compiler_name());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(2U, output_files.size());
  EXPECT_EQ("world", output_files[0]);
  EXPECT_EQ("hello.dwo", output_files[1]);

  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ(CompilerFlagType::Gcc, flags.type());
}

TEST_F(GCCFlagsTest, FissionJustLink) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-gsplit-dwarf");
  args.push_back("-o");
  args.push_back("world");
  args.push_back("hello.o");

  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::LINK, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("gcc", flags.compiler_base_name());
  EXPECT_EQ("gcc", flags.compiler_name());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(1U, output_files.size());
  EXPECT_EQ("world", output_files[0]);

  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ(CompilerFlagType::Gcc, flags.type());
}

TEST_F(GCCFlagsTest, ClangBaseName) {
  std::vector<string> args;
  args.push_back(
      "/usr/src/chromium/src/"
      "third_party/llvm-build/Release+Assets/bin/clang");
  GCCFlags flags(args, "/");
  EXPECT_EQ("clang", flags.compiler_base_name());
  EXPECT_EQ("clang", flags.compiler_name());
  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
}

TEST_F(GCCFlagsTest, ClangxxBaseName) {
  std::vector<string> args;
  args.push_back(
      "/usr/src/chromium/src/"
      "third_party/llvm-build/Release+Assets/bin/clang++");
  GCCFlags flags(args, "/");
  EXPECT_EQ("clang++", flags.compiler_base_name());
  EXPECT_EQ("clang++", flags.compiler_name());
  EXPECT_TRUE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
}

TEST_F(GCCFlagsTest, PnaclClangBaseName) {
  std::vector<string> args;
  args.push_back("toolchain/linux_x86_pnacl/newlib/bin/pnacl-clang");
  GCCFlags flags(args, "/");
  EXPECT_EQ("pnacl-clang", flags.compiler_base_name());
  EXPECT_EQ("clang", flags.compiler_name());
  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
}

TEST_F(GCCFlagsTest, PnaclClangxxBaseName) {
  std::vector<string> args;
  args.push_back("toolchain/linux_x86_pnacl/newlib/bin/pnacl-clang++");
  GCCFlags flags(args, "/");
  EXPECT_EQ("pnacl-clang++", flags.compiler_base_name());
  EXPECT_EQ("clang++", flags.compiler_name());
  EXPECT_TRUE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
}

TEST_F(GCCFlagsTest, GccPipe) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-o");
  args.push_back("hello.o");
  args.push_back("-pipe");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, "/");
  EXPECT_TRUE(flags.has_pipe());
}

TEST_F(GCCFlagsTest, GccFfreestanding) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-o");
  args.push_back("hello.o");
  args.push_back("-ffreestanding");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, "/");
  EXPECT_TRUE(flags.has_ffreestanding());
  EXPECT_FALSE(flags.has_fno_hosted());
  EXPECT_FALSE(flags.has_fsyntax_only());
  std::vector<string> want_compiler_info_flags;
  want_compiler_info_flags.push_back("-ffreestanding");
  EXPECT_EQ(want_compiler_info_flags, flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, GccFnohosted) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-o");
  args.push_back("hello.o");
  args.push_back("-fno-hosted");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, "/");
  EXPECT_FALSE(flags.has_ffreestanding());
  EXPECT_TRUE(flags.has_fno_hosted());
  EXPECT_FALSE(flags.has_fsyntax_only());
  std::vector<string> want_compiler_info_flags;
  want_compiler_info_flags.push_back("-fno-hosted");
  EXPECT_EQ(want_compiler_info_flags, flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, GccWrapper) {
  // See https://gcc.gnu.org/wiki/DebuggingGCC
  // $ gcc <parameters> -wrapper gdb,--args
  // $ gcc <parameters> -wrapper valgrind
  std::vector<string> origs{
      "gcc", "-o", "hello.o", "-c", "hello.c",
  };

  {
    GCCFlags flags(origs, "/");
    EXPECT_FALSE(flags.has_wrapper());
  }
  {
    std::vector<string> args(origs);
    args.insert(args.end(), {"-wrapper", "valgrind"});
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.has_wrapper());
  }
}

TEST_F(GCCFlagsTest, GccFplugin) {
  std::vector<string> origs{
      "gcc", "-o", "hello.o", "-c", "helloc",
  };

  {
    GCCFlags flags(origs, "/");
    EXPECT_FALSE(flags.has_fplugin());
  }

  {
    std::vector<string> args(origs);
    args.insert(args.end(), {"-fplugin=foo.so"});
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.has_fplugin());
  }
}

TEST_F(GCCFlagsTest, GccUndef) {
  std::vector<string> origs{
      "gcc", "-undef", "-c", "hello.c",
  };

  GCCFlags flags(origs, "/");

  std::vector<string> want_compiler_info_flags{
      "-undef",
  };
  EXPECT_EQ(want_compiler_info_flags, flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, ClangFSyntaxOnly) {
  std::vector<string> args;
  args.push_back("clang");
  args.push_back("-o");
  args.push_back("hello.o");
  args.push_back("-fsyntax-only");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, "/");
  EXPECT_TRUE(flags.has_fsyntax_only());
  EXPECT_FALSE(flags.has_fno_hosted());
  EXPECT_FALSE(flags.has_ffreestanding());
  std::vector<string> want_compiler_info_flags;
  want_compiler_info_flags.push_back("-fsyntax-only");
  EXPECT_EQ(want_compiler_info_flags, flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, ClangFprofileInstrGenerate) {
  std::vector<string> args{
      "clang", "-o", "hello.o", "-fprofile-instr-generate", "-c", "hello.c"};
  GCCFlags flags(args, "/");

  std::vector<string> want_compiler_info_flags{"-fprofile-instr-generate"};
  EXPECT_EQ(want_compiler_info_flags, flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, ClangXoption) {
  std::vector<string> args;
  args.push_back("clang");
  args.push_back("-o");
  args.push_back("hello.o");
  args.push_back("-Xclang");
  args.push_back("-load");
  args.push_back("-Xclang");
  args.push_back(
      "/usr/src/chromium/src/tools/clang/scripts/../../../"
      "third_party/llvm-build/Release+Asserts/lib/"
      "libFindBadConstructs.so");
  args.push_back("-Xclang");
  args.push_back("-add-plugin");
  args.push_back("-Xclang");
  args.push_back("find-bad-constructs");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, "/");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();
  EXPECT_EQ("clang", flags.compiler_base_name());
  EXPECT_EQ("clang", flags.compiler_name());
  ASSERT_EQ(1U, flags.input_filenames().size());
  EXPECT_EQ("hello.c", flags.input_filenames()[0]);
  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(1U, output_files.size());
  EXPECT_EQ("hello.o", output_files[0]);
}

TEST_F(GCCFlagsTest, ClangNoIntegratedAs) {
  // -no-integrated-as
  std::vector<string> args;
  args.push_back("clang");
  args.push_back("-no-integrated-as");
  GCCFlags flags(args, "/");
  EXPECT_EQ("clang", flags.compiler_base_name());
  EXPECT_EQ("clang", flags.compiler_name());
  EXPECT_TRUE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());

  const std::vector<string>& compiler_info_flags = flags.compiler_info_flags();
  ASSERT_EQ(1UL, compiler_info_flags.size());
  EXPECT_EQ("-no-integrated-as", compiler_info_flags[0]);
}

TEST_F(GCCFlagsTest, ClangFnoIntegratedAs) {
  // -fno-integrated-as
  std::vector<string> args;
  args.push_back("clang");
  args.push_back("-fno-integrated-as");
  GCCFlags flags(args, "/");
  EXPECT_EQ("clang", flags.compiler_base_name());
  EXPECT_EQ("clang", flags.compiler_name());
  EXPECT_TRUE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.is_cplusplus());
  EXPECT_FALSE(flags.has_nostdinc());

  const std::vector<string>& compiler_info_flags = flags.compiler_info_flags();
  ASSERT_EQ(1UL, compiler_info_flags.size());
  EXPECT_EQ("-fno-integrated-as", compiler_info_flags[0]);
}

TEST_F(GCCFlagsTest, PnaclClangPnaclBias) {
  std::vector<string> args;
  const string& pnacl_command = "/tmp/pnacl-clang++";
  ASSERT_TRUE(GCCFlags::IsPNaClClangCommand(pnacl_command));
  args.push_back(pnacl_command);
  args.push_back("--pnacl-bias=x86-32-nonsfi");
  GCCFlags flags(args, "/");
  EXPECT_EQ("clang++", flags.compiler_name());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("--pnacl-bias=x86-32-nonsfi");
  EXPECT_EQ(expected_compiler_info_flags, flags.compiler_info_flags());

  // --pnacl-arm-bias
  args[1] = "--pnacl-arm-bias";
  GCCFlags flags_arm(args, "/");
  expected_compiler_info_flags[0] = "--pnacl-arm-bias";
  EXPECT_EQ(expected_compiler_info_flags, flags_arm.compiler_info_flags());

  // --pnacl-mips-bias
  args[1] = "--pnacl-mips-bias";
  GCCFlags flags_mips(args, "/");
  expected_compiler_info_flags[0] = "--pnacl-mips-bias";
  EXPECT_EQ(expected_compiler_info_flags, flags_mips.compiler_info_flags());

  // --pnacl-i686-bias
  args[1] = "--pnacl-i686-bias";
  GCCFlags flags_i686(args, "/");
  expected_compiler_info_flags[0] = "--pnacl-i686-bias";
  EXPECT_EQ(expected_compiler_info_flags, flags_i686.compiler_info_flags());

  // --pnacl-x86_64-bias
  args[1] = "--pnacl-x86_64-bias";
  GCCFlags flags_x86_64(args, "/");
  expected_compiler_info_flags[0] = "--pnacl-x86_64-bias";
  EXPECT_EQ(expected_compiler_info_flags, flags_x86_64.compiler_info_flags());
}

TEST_F(GCCFlagsTest, PnaclClangPnaclBiasShouldNotBeDetectedByClang) {
  std::vector<string> args;
  args.push_back("/tmp/clang++");
  args.push_back("--pnacl-bias=x86-32-nonsfi");
  GCCFlags flags(args, "/");
  EXPECT_EQ("clang++", flags.compiler_base_name());
  EXPECT_EQ("clang++", flags.compiler_name());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, ModeAndOutputFiles) {
  const struct {
    std::vector<string> opts;
    GCCFlags::Mode expected_mode;
    std::vector<string> expected_outputs;
  } kTestCases[] = {
      {{"-c"}, GCCFlags::COMPILE, {"hello.o"}},
      {{"-S"}, GCCFlags::COMPILE, {"hello.s"}},
      {{"-E"}, GCCFlags::PREPROCESS, {}},
      {{"-M"}, GCCFlags::PREPROCESS, {}},
      {{"-M", "-c"}, GCCFlags::PREPROCESS, {}},
      {{"-M", "-MF", "hello.d"}, GCCFlags::PREPROCESS, {"hello.d"}},
      {{"-MM", "-MF", "hello.d"}, GCCFlags::PREPROCESS, {"hello.d"}},
      {{"-E", "-M", "-MF", "hello.d", "-c"}, GCCFlags::PREPROCESS, {"hello.d"}},
      {{"-E", "-MM", "-MF", "hello.d", "-c"},
       GCCFlags::PREPROCESS,
       {"hello.d"}},
      {{"-MD", "-MF", "hello.d", "-c"},
       GCCFlags::COMPILE,
       {"hello.d", "hello.o"}},
      {{"-MMD", "-MF", "hello.d", "-c"},
       GCCFlags::COMPILE,
       {"hello.d", "hello.o"}},
      {{"-E", "-c"}, GCCFlags::PREPROCESS, {}},
      {{"-c", "-M"}, GCCFlags::PREPROCESS, {}},
      {{"-c", "-E"}, GCCFlags::PREPROCESS, {}},
      {{"-S", "-M"}, GCCFlags::PREPROCESS, {}},
      {{"-M", "-S"}, GCCFlags::PREPROCESS, {}},
      {{"-c", "-S"}, GCCFlags::COMPILE, {"hello.s"}},
      {{"-S", "-c"}, GCCFlags::COMPILE, {"hello.s"}},
  };

  for (const auto& tc : kTestCases) {
    std::vector<string> args;
    args.push_back("gcc");
    std::copy(tc.opts.begin(), tc.opts.end(), back_inserter(args));
    args.push_back("hello.c");

    GCCFlags flags(args, "/");

    std::vector<string> outputs = flags.output_files();
    std::sort(outputs.begin(), outputs.end());

    EXPECT_EQ(tc.expected_mode, flags.mode()) << args;
    EXPECT_EQ(tc.expected_outputs, outputs) << args;
  }
}

TEST_F(GCCFlagsTest, PrintFileName) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("-print-file-name");
  args.push_back("hello.c");

  GCCFlags flags(args, "/");
  EXPECT_FALSE(flags.is_successful());
  EXPECT_FALSE(flags.is_stdin_input());
  EXPECT_FALSE(flags.is_cplusplus());
}

TEST_F(GCCFlagsTest, Stdin) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("-xc++");
  args.push_back("-");
  {
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.is_successful());
    EXPECT_TRUE(flags.is_stdin_input());
  }

  args.pop_back();
  args.push_back("/dev/stdin");
  {
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.is_successful());
    EXPECT_TRUE(flags.is_stdin_input());
  }
}

TEST_F(GCCFlagsTest, Profile) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("hello.c");
  args.push_back("-fprofile-dir=foo");

  // fprofile-use isn't set yet.
  {
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.is_successful());
    EXPECT_TRUE(flags.optional_input_filenames().empty());
  }
  // Now -fprofile-use is specified.
  args.push_back("-fprofile-use");
  {
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1, static_cast<int>(flags.optional_input_filenames().size()));
#ifndef _WIN32
    EXPECT_EQ("foo/hello.gcda", flags.optional_input_filenames()[0]);
#else
    EXPECT_EQ("foo\\hello.gcda", flags.optional_input_filenames()[0]);
#endif
  }

  // The output directory should have been changed.
  args.push_back("-fprofile-generate=bar");
  {
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1, static_cast<int>(flags.optional_input_filenames().size()));
#ifndef _WIN32
    EXPECT_EQ("bar/hello.gcda", flags.optional_input_filenames()[0]);
#else
    EXPECT_EQ("bar\\hello.gcda", flags.optional_input_filenames()[0]);
#endif
  }
}

TEST_F(GCCFlagsTest, ProfileCwd) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("foo/hello.c");
  args.push_back("-fprofile-use");

  // We'll check .gcda files in the current directory.
  args.push_back("-fprofile-use");
  {
#ifndef _WIN32
    GCCFlags flags(args, "/tmp");
#else
    GCCFlags flags(args, "C:\\tmp");
#endif
    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1, static_cast<int>(flags.optional_input_filenames().size()));

    EXPECT_EQ(file::JoinPath(".", "hello.gcda"),
              flags.optional_input_filenames()[0]);
  }
}

TEST_F(GCCFlagsTest, ProfileDir) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("foo/hello.c");

  args.push_back("-fprofile-dir=foo");
  args.push_back("-fprofile-use=hello.prof");

#ifndef _WIN32
  GCCFlags flags(args, "/tmp");
#else
  GCCFlags flags(args, "C:\\tmp");
#endif
  EXPECT_TRUE(flags.is_successful());
  ASSERT_EQ(2U, flags.optional_input_filenames().size());

  EXPECT_EQ(file::JoinPath("foo", "hello.prof"),
            flags.optional_input_filenames()[0]);
  EXPECT_EQ(file::JoinPath("foo", "hello.gcda"),
            flags.optional_input_filenames()[1]);
}

TEST_F(GCCFlagsTest, ProfileClang) {
  {
    // prof abs dir case
    std::vector<string> args;
    args.push_back("clang");
    args.push_back("-c");
    args.push_back("foo/hello.c");

    const string& prof_dir = file::JoinPath(tmp_dir_, "hello.profdata");
    ASSERT_TRUE(file::CreateDir(prof_dir, file::CreationMode(0777)).ok());

    args.push_back("-fprofile-use=" + prof_dir);

#ifndef _WIN32
    GCCFlags flags(args, "/tmp");
#else
    GCCFlags flags(args, "C:\\tmp");
#endif
    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1U, flags.optional_input_filenames().size());

    EXPECT_EQ(file::JoinPath(prof_dir, "default.profdata"),
              flags.optional_input_filenames()[0]);
    ASSERT_TRUE(RecursivelyDelete(prof_dir, file::Defaults()).ok());
  }

  {
    // prof rel dir case
    std::vector<string> args;
    args.push_back("clang");
    args.push_back("-c");
    args.push_back("foo/hello.c");

    args.push_back("-fprofile-use=foo");

    const string& prof_dir = file::JoinPath(tmp_dir_, "foo");
    ASSERT_TRUE(file::CreateDir(prof_dir, file::CreationMode(0777)).ok());
    GCCFlags flags(args, tmp_dir_);

    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1U, flags.optional_input_filenames().size());

    EXPECT_EQ(file::JoinPath(".", "foo", "default.profdata"),
              flags.optional_input_filenames()[0]);

    ASSERT_TRUE(RecursivelyDelete(prof_dir, file::Defaults()).ok());
  }

  {
    // abs prof file case
    std::vector<string> args;
    args.push_back("clang");
    args.push_back("-c");
    args.push_back("foo/hello.c");

    const string& prof_file = file::JoinPath(tmp_dir_, "hello.profdata");
    args.push_back("-fprofile-use=" + prof_file);

#ifndef _WIN32
    GCCFlags flags(args, "/tmp");
#else
    GCCFlags flags(args, "C:\\tmp");
#endif
    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1U, flags.optional_input_filenames().size());

    EXPECT_EQ(prof_file, flags.optional_input_filenames()[0]);
  }

  {
    // relative prof file case
    std::vector<string> args;
    args.push_back("clang");
    args.push_back("-c");
    args.push_back("foo/hello.c");

    args.push_back("-fprofile-use=hello.profdata");

#ifndef _WIN32
    GCCFlags flags(args, "/tmp");
#else
    GCCFlags flags(args, "C:\\tmp");
#endif
    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1U, flags.optional_input_filenames().size());

    EXPECT_EQ(file::JoinPath(".", "hello.profdata"),
              flags.optional_input_filenames()[0]);
  }
}

TEST_F(GCCFlagsTest, AtFile) {
  std::vector<string> args;
  args.push_back("gcc");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back("@" + at_file);

  // The at-file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  ASSERT_TRUE(WriteStringToFile("-c -DFOO '-DBAR=\"a b\\c\"' foo.cc", at_file));
  flags = CompilerFlagsParser::MustNew(args, ".");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("gcc", flags->compiler_name());
  EXPECT_EQ(5U, flags->expanded_args().size());
  EXPECT_EQ("gcc", flags->expanded_args()[0]);
  EXPECT_EQ("-c", flags->expanded_args()[1]);
  EXPECT_EQ("-DFOO", flags->expanded_args()[2]);
  EXPECT_EQ("-DBAR=\"a b\\c\"", flags->expanded_args()[3]);
  EXPECT_EQ("foo.cc", flags->expanded_args()[4]);
  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);
  ASSERT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ(PathResolver::PlatformConvert(at_file),
            flags->optional_input_filenames()[0]);

  ASSERT_TRUE(
      WriteStringToFile(" -c -DFOO '-DBAR=\"a b\\c\"' \n foo.cc\n", at_file));
  flags = CompilerFlagsParser::MustNew(args, ".");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("gcc", flags->compiler_name());
  EXPECT_EQ(5U, flags->expanded_args().size());
  EXPECT_EQ("gcc", flags->expanded_args()[0]);
  EXPECT_EQ("-c", flags->expanded_args()[1]);
  EXPECT_EQ("-DFOO", flags->expanded_args()[2]);
  EXPECT_EQ("-DBAR=\"a b\\c\"", flags->expanded_args()[3]);
  EXPECT_EQ("foo.cc", flags->expanded_args()[4]);
  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);
  ASSERT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ(PathResolver::PlatformConvert(at_file),
            flags->optional_input_filenames()[0]);
}

TEST_F(GCCFlagsTest, Idirafter) {
  std::vector<string> args;
  args.push_back("g++");
  args.push_back("-idirafter");
  args.push_back("include");
  args.push_back("-c");
  args.push_back("foo.cc");

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  ASSERT_EQ(2U, flags.compiler_info_flags().size());
  EXPECT_EQ("-idirafter", flags.compiler_info_flags()[0]);
  EXPECT_EQ("include", flags.compiler_info_flags()[1]);
}

TEST_F(GCCFlagsTest, PreprocessFlags) {
  // Note: g++ may error on following code due to unknown flags.
  const std::vector<string> args{
      "g++",
      "-c",
      "foo.cc",
      "-Wp,-Dfoo=bar,-Ufoo2",
      "-Ufoo",
      "-Dfoo2=bar2",
      "-Ufoo3",
      "-Wp,-Dfoo3=bar3",
      "-Wp,-Dfoo4=bar4,-Ufoo4",
      "-Wp,-MD,deps/foobar.d",
      "-Wp,-unknown1,-unknown2",
      "-Wp,-unknown3",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());

  const std::vector<std::pair<string, bool>> expected_macros{
      {"foo", false},      {"foo2=bar2", true}, {"foo3", false},
      {"foo=bar", true},   {"foo2", false},     {"foo3=bar3", true},
      {"foo4=bar4", true}, {"foo4", false},
  };
  EXPECT_EQ(expected_macros, flags.commandline_macros());

  const std::vector<string> expected_output_files{
      "foo.o", "deps/foobar.d",
  };
  EXPECT_EQ(expected_output_files, flags.output_files());

  const std::vector<string> expected_unknown_flags{
      "-Wp,-unknown1", "-Wp,-unknown2", "-Wp,-unknown3",
  };
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, LinkerFlags) {
  const std::vector<string> args{
      "g++", "-Wl,--start-group", "-Wl,--end-group", "-Wl,--threads", "foo.c",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());

  // all -Wl, are treated as unknown for now.
  const std::vector<string> expected_unknown_flags{
      "-Wl,--start-group", "-Wl,--end-group", "-Wl,--threads",
  };
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, AssemblerFlags) {
  const std::vector<string> args{
      "g++",
      "-Wa,--noexecstack",
      "-Wa,--defsym,STEREO_OUTPUT",
      "-Wa,--defsym",
      "-Wa,FOO",
      "-Wa,-Iout/somewhere",
      "-Wa,-gdwarf-2",
      "-Wa,-march=foo",
      "-Wa,-march,foo",
      "-Wa,-mfpu=neon",
      "-c",
      "foo.c",
      "-Wa,-unknown1,-unknown2",
      "-Wa,-unknown3",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());

  const std::vector<string> expected_unknown_flags{
      "-Wa,-unknown1", "-Wa,-unknown2", "-Wa,-unknown3",
  };
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, MixW) {
  const std::vector<string> args{
      "g++",
      "-c",
      "foo.c",
      "-Wall",
      "-W",
      "-Wextra",
      "-Wno-div-by-zero",
      "-Wunknown",
      "-Wp,-Dfoo=bar,-Ufoo",
      "-Wa,--noexecstack",
      "-Wl,--defsym,STEREO_OUTPUT",
      "-Wl,--defsym",
      "-Wl,FOO",
      "-Wa,-unknown1,-unknown2",
      "-Wl,-unknown3",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());

  const std::vector<string> expected_unknown_flags{
      "-Wa,-unknown1", "-Wa,-unknown2", "-Wl,--defsym,STEREO_OUTPUT",
      "-Wl,--defsym",  "-Wl,FOO",       "-Wl,-unknown3",
      "-Wunknown",
  };
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, MD) {
  std::vector<string> args;
  args.push_back("g++");
  args.push_back("-MD");
  args.push_back("-c");
  args.push_back("foo.cc");

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());

  std::vector<string> output_files = flags.output_files();
  ASSERT_EQ(2U, output_files.size());
  std::sort(output_files.begin(), output_files.end());
  EXPECT_EQ("foo.d", output_files[0]);
  EXPECT_EQ("foo.o", output_files[1]);
}

TEST_F(GCCFlagsTest, MMD) {
  std::vector<string> args;
  args.push_back("g++");
  args.push_back("-MMD");
  args.push_back("-c");
  args.push_back("foo.cc");

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());

  std::vector<string> output_files = flags.output_files();
  ASSERT_EQ(2U, output_files.size());
  std::sort(output_files.begin(), output_files.end());
  EXPECT_EQ("foo.d", output_files[0]);
  EXPECT_EQ("foo.o", output_files[1]);
}

TEST_F(GCCFlagsTest, SystemHeaderPrefix) {
  const std::vector<string> args{
      "clang++",
      "-c",
      "foo.cc",
      "--system-header-prefix=a",
      "--system-header-prefix",
      "b",
      "--no-system-header-prefix=c",
  };

  const std::vector<string> expected_input_files{
      "foo.cc",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_EQ(expected_input_files, flags.input_filenames());
}

TEST_F(GCCFlagsTest, DebugFlags) {
  const std::vector<string> args{
      "g++",
      "-c",
      "foo.cc",
      "-g",
      "-g0",
      "-g1",
      "-g2",
      "-g3",
      "-gcolumn-info",
      "-gdw",
      "-gdwarf-2",
      "-gdwarf-3",
      "-ggdb3",
      "-ggnu-pubnames",
      "-gline-tables-only",
      "-gsplit-dwarf",
      "-gunknown",
  };
  const std::vector<string> expected_unknown_flags{
      "-gunknown",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, UnknownFlags) {
  const std::vector<string> args{
      "g++", "-c", "foo.cc", "-unknown1", "--unknown2",
  };
  const std::vector<string> expected{
      "-unknown1", "--unknown2",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(expected, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, KnownWarningOptions) {
  // -W
  EXPECT_TRUE(GCCFlags::IsKnownWarningOption(""));

  // -Waddress
  EXPECT_TRUE(GCCFlags::IsKnownWarningOption("address"));

  // -Wunknown (no such options)
  EXPECT_FALSE(GCCFlags::IsKnownWarningOption("unknown"));

  // -Walloc-size-larger-than=100
  EXPECT_TRUE(GCCFlags::IsKnownWarningOption("alloc-size-larger-than=100"));

  // -Wnormalized. This needs "=n"
  EXPECT_FALSE(GCCFlags::IsKnownWarningOption("normalized"));

  // Check with removing no-.
  // no-bool-compare is not defined in kKnownWarningOptions, but
  // bool-compare is.
  ASSERT_TRUE(std::binary_search(std::begin(kKnownWarningOptions),
                                 std::end(kKnownWarningOptions),
                                 string("bool-compare")));
  ASSERT_FALSE(std::binary_search(std::begin(kKnownWarningOptions),
                                  std::end(kKnownWarningOptions),
                                  string("no-bool-compare")));
  EXPECT_TRUE(GCCFlags::IsKnownWarningOption("no-bool-compare"));
}

TEST_F(GCCFlagsTest, WithoutOoption) {
  const std::vector<string> args{
      "g++", "-c", "/tmp/foo.cc",
  };
  const std::vector<string> expected_output_files{
      "foo.o",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(expected_output_files, flags.output_files());
}

TEST_F(GCCFlagsTest, WithoutOoptionLink) {
  const std::vector<string> args{
      "g++", "/tmp/foo.cc",
  };
  const std::vector<string> expected_output_files{
      "a.out",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(expected_output_files, flags.output_files());
}

TEST_F(GCCFlagsTest, ClangSanitize) {
  const std::vector<string> args{
      "clang++", "-c", "foo.cc", "-o", "foo.o", "-fsanitize=address",
      "-fsanitize=thread",
      "-fsanitize-blacklist=dummy1.txt",
      "-fno-sanitize-blacklist",
      "-fsanitize-blacklist=dummy2.txt",
  };

  std::set<string> expected_sanitize{
    "address", "thread",
  };

  std::vector<string> expected_optional_input_files{
    "dummy1.txt", "dummy2.txt",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(expected_sanitize, flags.fsanitize());
  EXPECT_TRUE(flags.has_fno_sanitize_blacklist());
  EXPECT_EQ(expected_optional_input_files, flags.optional_input_filenames());
}

TEST_F(GCCFlagsTest, GetFirstLine) {
  EXPECT_EQ(
      "gcc (Ubuntu 4.4.3-4ubuntu5) 4.4.3",
      GetFirstLine("gcc (Ubuntu 4.4.3-4ubuntu5) 4.4.3\n"
                   "Copyright (C) 2009 Free Software Foundation, Inc.\n"));
}

TEST_F(GCCFlagsTest, NormalizeGccVersion) {
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion("gcc (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion("cc (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion("g++ (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion("c++ (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ(
      "(Native Client SDK [438be0db920e3ca7711844c0218a5db37c747c2b]) "
      "4.8.1",
      NormalizeGccVersion("arm-nacl-gcc (Native Client SDK "
                          "[438be0db920e3ca7711844c0218a5db37c747c2b]) 4.8.1"));
  EXPECT_EQ("clang version 3.0 (trunk 129729)",
            NormalizeGccVersion("clang version 3.0 (trunk 129729)"));
  EXPECT_EQ("clang++ version 3.0 (trunk 129729)",
            NormalizeGccVersion("clang++ version 3.0 (trunk 129729)"));
}

TEST_F(GCCFlagsTest, GccFlags) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("hello.c");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.o", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.c", flags->input_filenames()[0]);
  EXPECT_EQ("gcc", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("gcc", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/tmp", flags->cwd());

  const size_t env_array_length = 9;
  const char** env =
      static_cast<const char**>(malloc(sizeof(const char*) * env_array_length));
  env[0] = strdup("PATH=/usr/bin:/bin");
  env[1] = strdup("LIBRARY_PATH=../libsupp");
  env[2] = strdup("CPATH=.:/special/include");
  env[3] = strdup("C_INCLUDE_PATH=.:/special/include");
  env[4] = strdup("CPLUS_INCLUDE_PATH=.:/special/include/c++");
  env[5] = strdup("OBJC_INCLUDE_PATH=./special/include/objc");
  env[6] = strdup("DEPENDENCIES_OUTPUT=foo.d");
  env[7] = strdup("SUNPRO_DEPENDENCIES=foo.d");
  env[8] = nullptr;

  std::vector<string> important_env;
  flags->GetClientImportantEnvs(env, &important_env);

  std::vector<string> expected_env;
  expected_env.push_back("LIBRARY_PATH=../libsupp");
  expected_env.push_back("CPATH=.:/special/include");
  expected_env.push_back("C_INCLUDE_PATH=.:/special/include");
  expected_env.push_back("CPLUS_INCLUDE_PATH=.:/special/include/c++");
  expected_env.push_back("OBJC_INCLUDE_PATH=./special/include/objc");
  expected_env.push_back("DEPENDENCIES_OUTPUT=foo.d");
  expected_env.push_back("SUNPRO_DEPENDENCIES=foo.d");
  EXPECT_EQ(expected_env, important_env);

  for (size_t i = 0; i < env_array_length; ++i) {
    if (env[i] != nullptr) {
      free(const_cast<char*>(env[i]));
    }
  }
  free(env);

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  std::vector<string> compiler_info_flags;
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ(devtools_goma::GCCFlags::COMPILE, gcc_flags->mode());
  EXPECT_EQ("", gcc_flags->isysroot());
  EXPECT_FALSE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_FALSE(gcc_flags->has_pipe());
}

TEST_F(GCCFlagsTest, ClangImportantEnv) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("hello.c");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));

  const size_t env_array_length = 8;
  const char** env =
      static_cast<const char**>(malloc(sizeof(const char*) * env_array_length));
  env[0] = strdup("PATH=/usr/bin:/bin");
  env[1] = strdup("LIBRARY_PATH=../libsupp");
  env[2] = strdup("CPATH=.:/special/include");
  env[3] = strdup("C_INCLUDE_PATH=.:/special/include");
  env[4] = strdup("MACOSX_DEPLOYMENT_TARGET=10.7");
  env[5] = strdup("SDKROOT=/tmp/path_to_root");
  env[6] = strdup("DEVELOPER_DIR=/tmp/path_to_developer_dir");
  env[7] = nullptr;

  std::vector<string> important_env;
  flags->GetClientImportantEnvs(env, &important_env);

  std::vector<string> expected_env;
  expected_env.push_back("LIBRARY_PATH=../libsupp");
  expected_env.push_back("CPATH=.:/special/include");
  expected_env.push_back("C_INCLUDE_PATH=.:/special/include");
  expected_env.push_back("MACOSX_DEPLOYMENT_TARGET=10.7");
  expected_env.push_back("SDKROOT=/tmp/path_to_root");
  expected_env.push_back("DEVELOPER_DIR=/tmp/path_to_developer_dir");
  EXPECT_EQ(expected_env, important_env);

  for (size_t i = 0; i < env_array_length; ++i) {
    if (env[i] != nullptr) {
      free(const_cast<char*>(env[i]));
    }
  }
  free(env);
}

TEST_F(GCCFlagsTest, IsImportantEnvGCC) {
  const struct {
    const char* env;
    const bool client_important;
    const bool server_important;
  } kTestCases[] {
    { "LIBRARY_PATH=../libsupp", true, true },
    { "CPATH=.:/special/include", true, true },
    { "C_INCLUDE_PATH=.:/include", true, true },
    { "CPLUS_INCLUDE_PATH=.:/include", true, true },
    { "DEPENDENCIES_OUTPUT=/tmp/to", true, true },
    { "SUNPRO_DEPENDENCIES=/tmp/to", true, true },
    { "MACOSX_DEPLOYMENT_TARGET=/tmp/to", true, true },
    { "SDKROOT=/tmp/to", true, true },
    { "PWD=/tmp/to", true, true },
    { "DEVELOPER_DIR=/tmp/to", true, true },

    { "PATHEXT=.EXE", true, false },
    { "pathext=.EXE", true, false },
    { "SystemRoot=C:\\Windows", true, false },
    { "systemroot=C:\\Windows", true, false },

    { "SystemDrive=C:", false, false },
    { "systemdrive=C:", false, false },
    { "LD_PRELOAD=foo.so", false, false },
    { "ld_preload=foo.so", false, false },
  };

  std::vector<string> args {
    "gcc", "-c", "hello.c",
  };
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));

  for (const auto& tc : kTestCases) {
    ASSERT_TRUE(!tc.server_important || tc.client_important);
    EXPECT_EQ(flags->IsClientImportantEnv(tc.env), tc.client_important)
        << tc.env;
    EXPECT_EQ(flags->IsServerImportantEnv(tc.env), tc.server_important)
        << tc.env;
  }
}

TEST_F(GCCFlagsTest, ChromeLinuxCompileFlag) {
  std::vector<string> args;
  args.push_back("g++");
  args.push_back("-DNO_HEAPCHECKER");
  args.push_back("-DENABLE_REMOTING=1");
  args.push_back("-I.");
  args.push_back("-Igpu");
  args.push_back("-Ithird_party/sqlite");
  args.push_back("-Werror");
  args.push_back("-pthread");
  args.push_back("-fno-exceptions");
  args.push_back("-Wall");
  args.push_back("-Wno-unused-parameter");
  args.push_back("-Wno-missing-field-initializers");
  args.push_back("-fvisibility=hidden");
  args.push_back("-pipe");
  args.push_back("-fPIC");
  args.push_back("-fno-strict-aliasing");
  args.push_back("-I/usr/include/nss");
  args.push_back("-O2");
  args.push_back("-fno-ident");
  args.push_back("-fdata-sections");
  args.push_back("-ffunction-sections");
  args.push_back("-fno-rtti");
  args.push_back("-fno-threadsafe-statics");
  args.push_back("-fvisibility-inlines-hidden");
  args.push_back("-MMD");
  args.push_back("-MF");
  args.push_back("out/Release/.deps/out/Release/obj.target/"
                 "chrome/chrome/app/chrome_main.o.d.raw");
  args.push_back("-c");
  args.push_back("-o");
  args.push_back("out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  args.push_back("chrome/app/chrome_main.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/local/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(2U, flags->output_files().size());
  ExpectHasElement(flags->output_files(),
                   "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  ExpectHasElement(flags->output_files(),
                   "out/Release/.deps/out/Release/obj.target/"
                   "chrome/chrome/app/chrome_main.o.d.raw");
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("chrome/app/chrome_main.cc", flags->input_filenames()[0]);
  EXPECT_EQ("g++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("g++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/local/src", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("-pthread");
  compiler_info_flags.push_back("-fno-exceptions");
  compiler_info_flags.push_back("-fvisibility=hidden");
  compiler_info_flags.push_back("-fPIC");
  compiler_info_flags.push_back("-fno-strict-aliasing");
  compiler_info_flags.push_back("-O2");
  compiler_info_flags.push_back("-fno-ident");
  compiler_info_flags.push_back("-fdata-sections");
  compiler_info_flags.push_back("-ffunction-sections");
  compiler_info_flags.push_back("-fno-rtti");
  compiler_info_flags.push_back("-fno-threadsafe-statics");
  compiler_info_flags.push_back("-fvisibility-inlines-hidden");
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ("", gcc_flags->isysroot());
  EXPECT_EQ(devtools_goma::GCCFlags::COMPILE, gcc_flags->mode());
  EXPECT_TRUE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_TRUE(gcc_flags->has_pipe());
  ASSERT_EQ(4, static_cast<int>(gcc_flags->include_dirs().size()));
  EXPECT_EQ(".", gcc_flags->include_dirs()[0]);
  EXPECT_EQ("gpu", gcc_flags->include_dirs()[1]);
  EXPECT_EQ("third_party/sqlite", gcc_flags->include_dirs()[2]);
  EXPECT_EQ("/usr/include/nss", gcc_flags->include_dirs()[3]);
  ASSERT_EQ(4U, gcc_flags->non_system_include_dirs().size());
  EXPECT_EQ(".", gcc_flags->non_system_include_dirs()[0]);
  EXPECT_EQ("gpu", gcc_flags->non_system_include_dirs()[1]);
  EXPECT_EQ("third_party/sqlite", gcc_flags->non_system_include_dirs()[2]);
  EXPECT_EQ("/usr/include/nss", gcc_flags->non_system_include_dirs()[3]);
  ASSERT_EQ(0U, gcc_flags->root_includes().size());
  ASSERT_EQ(0U, gcc_flags->framework_dirs().size());
  ASSERT_EQ(2U, gcc_flags->commandline_macros().size());
  EXPECT_EQ("NO_HEAPCHECKER", gcc_flags->commandline_macros()[0].first);
  EXPECT_TRUE(gcc_flags->commandline_macros()[0].second);
  EXPECT_EQ("ENABLE_REMOTING=1", gcc_flags->commandline_macros()[1].first);
  EXPECT_TRUE(gcc_flags->commandline_macros()[1].second);
}

TEST_F(GCCFlagsTest, ChromeLinuxLinkFlag) {
  std::vector<string> args;
  args.push_back("g++");
  args.push_back("-pthread");
  args.push_back("-Wl,-z,noexecstack");
  args.push_back("-Lout/Release");
  args.push_back("-L/lib");
  args.push_back("-Wl,-uIsHeapProfilerRunning,-uProfilerStart");
  args.push_back("-Wl,-u_Z21InitialMallocHook_NewPKvj,"
                 "-u_Z22InitialMallocHook_MMapPKvS0_jiiix,"
                 "-u_Z22InitialMallocHook_SbrkPKvi");
  args.push_back("-Wl,-u_Z21InitialMallocHook_NewPKvm,"
                 "-u_Z22InitialMallocHook_MMapPKvS0_miiil,"
                 "-u_Z22InitialMallocHook_SbrkPKvl");
  args.push_back("-Wl,-O1");
  args.push_back("-Wl,--as-needed");
  args.push_back("-Wl,--gc-sections");
  args.push_back("-Wl,--icf=safe");
  args.push_back("-o");
  args.push_back("out/Release/chrome");
  args.push_back("-Wl,--start-group");
  args.push_back("out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  args.push_back("out/Release/obj.target/chrome/"
                 "chrome/app/chrome_main_posix.o");
  args.push_back("-Wl,--end-group");
  args.push_back("-lX11");
  args.push_back("-ldl");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/local/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("out/Release/chrome", flags->output_files()[0]);
  EXPECT_EQ(2U, flags->input_filenames().size());
  ExpectHasElement(flags->input_filenames(),
                   "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  ExpectHasElement(
      flags->input_filenames(),
      "out/Release/obj.target/chrome/chrome/app/chrome_main_posix.o");
  EXPECT_EQ("g++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("g++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/local/src", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("-pthread");
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ(devtools_goma::GCCFlags::LINK, gcc_flags->mode());
  EXPECT_EQ("", gcc_flags->isysroot());
  EXPECT_TRUE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_FALSE(gcc_flags->has_pipe());
}

TEST_F(GCCFlagsTest, ChromeLinuxClangCompileFlag) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-fcolor-diagnostics");
  args.push_back("-DNO_HEAPCHECKER");
  args.push_back("-DENABLE_REMOTING=1");
  args.push_back("-I.");
  args.push_back("-Igpu");
  args.push_back("-Ithird_party/sqlite");
  args.push_back("-Werror");
  args.push_back("-pthread");
  args.push_back("-fno-exceptions");
  args.push_back("-Wall");
  args.push_back("-Wno-unused-parameter");
  args.push_back("-Wno-missing-field-initializers");
  args.push_back("-fvisibility=hidden");
  args.push_back("-pipe");
  args.push_back("-fPIC");
  args.push_back("-fno-strict-aliasing");
  args.push_back("-I/usr/include/nss");
  args.push_back("-O2");
  args.push_back("-fno-ident");
  args.push_back("-fdata-sections");
  args.push_back("-ffunction-sections");
  args.push_back("-fno-rtti");
  args.push_back("-fno-threadsafe-statics");
  args.push_back("-fvisibility-inlines-hidden");
  args.push_back("-MMD");
  args.push_back("-MF");
  args.push_back("out/Release/.deps/out/Release/obj.target/"
                 "chrome/chrome/app/chrome_main.o.d.raw");
  args.push_back("-c");
  args.push_back("-o");
  args.push_back("out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  args.push_back("chrome/app/chrome_main.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/local/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(2U, flags->output_files().size());
  ExpectHasElement(flags->output_files(),
                   "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  ExpectHasElement(flags->output_files(),
                   "out/Release/.deps/out/Release/obj.target/"
                   "chrome/chrome/app/chrome_main.o.d.raw");
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("chrome/app/chrome_main.cc", flags->input_filenames()[0]);
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/local/src", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("-fcolor-diagnostics");
  compiler_info_flags.push_back("-pthread");
  compiler_info_flags.push_back("-fno-exceptions");
  compiler_info_flags.push_back("-fvisibility=hidden");
  compiler_info_flags.push_back("-fPIC");
  compiler_info_flags.push_back("-fno-strict-aliasing");
  compiler_info_flags.push_back("-O2");
  compiler_info_flags.push_back("-fno-ident");
  compiler_info_flags.push_back("-fdata-sections");
  compiler_info_flags.push_back("-ffunction-sections");
  compiler_info_flags.push_back("-fno-rtti");
  compiler_info_flags.push_back("-fno-threadsafe-statics");
  compiler_info_flags.push_back("-fvisibility-inlines-hidden");
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ(devtools_goma::GCCFlags::COMPILE, gcc_flags->mode());
  EXPECT_EQ("", gcc_flags->isysroot());
  EXPECT_TRUE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_TRUE(gcc_flags->has_pipe());
  ASSERT_EQ(4, static_cast<int>(gcc_flags->include_dirs().size()));
  EXPECT_EQ(".", gcc_flags->include_dirs()[0]);
  EXPECT_EQ("gpu", gcc_flags->include_dirs()[1]);
  EXPECT_EQ("third_party/sqlite", gcc_flags->include_dirs()[2]);
  EXPECT_EQ("/usr/include/nss", gcc_flags->include_dirs()[3]);
  ASSERT_EQ(4U, gcc_flags->non_system_include_dirs().size());
  EXPECT_EQ(".", gcc_flags->non_system_include_dirs()[0]);
  EXPECT_EQ("gpu", gcc_flags->non_system_include_dirs()[1]);
  EXPECT_EQ("third_party/sqlite", gcc_flags->non_system_include_dirs()[2]);
  EXPECT_EQ("/usr/include/nss", gcc_flags->non_system_include_dirs()[3]);
  ASSERT_EQ(0U, gcc_flags->root_includes().size());
  ASSERT_EQ(0U, gcc_flags->framework_dirs().size());
  ASSERT_EQ(2U, gcc_flags->commandline_macros().size());
  EXPECT_EQ("NO_HEAPCHECKER", gcc_flags->commandline_macros()[0].first);
  EXPECT_TRUE(gcc_flags->commandline_macros()[0].second);
  EXPECT_EQ("ENABLE_REMOTING=1", gcc_flags->commandline_macros()[1].first);
  EXPECT_TRUE(gcc_flags->commandline_macros()[1].second);
}

TEST_F(GCCFlagsTest, ChromeLinuxClangLinkFlag) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-fcolor-diagnostics");
  args.push_back("-pthread");
  args.push_back("-Wl,-z,noexecstack");
  args.push_back("-Lout/Release");
  args.push_back("-L/lib");
  args.push_back("-Wl,-uIsHeapProfilerRunning,-uProfilerStart");
  args.push_back("-Wl,-u_Z21InitialMallocHook_NewPKvj,"
                 "-u_Z22InitialMallocHook_MMapPKvS0_jiiix,"
                 "-u_Z22InitialMallocHook_SbrkPKvi");
  args.push_back("-Wl,-u_Z21InitialMallocHook_NewPKvm,"
                 "-u_Z22InitialMallocHook_MMapPKvS0_miiil,"
                 "-u_Z22InitialMallocHook_SbrkPKvl");
  args.push_back("-Wl,-O1");
  args.push_back("-Wl,--as-needed");
  args.push_back("-Wl,--gc-sections");
  args.push_back("-Wl,--icf=safe");
  args.push_back("-o");
  args.push_back("out/Release/chrome");
  args.push_back("-Wl,--start-group");
  args.push_back("out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  args.push_back("out/Release/obj.target/chrome/"
                 "chrome/app/chrome_main_posix.o");
  args.push_back("-Wl,--end-group");
  args.push_back("-lX11");
  args.push_back("-ldl");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/local/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("out/Release/chrome", flags->output_files()[0]);
  EXPECT_EQ(2U, flags->input_filenames().size());
  ExpectHasElement(flags->input_filenames(),
                   "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  ExpectHasElement(
      flags->input_filenames(),
      "out/Release/obj.target/chrome/chrome/app/chrome_main_posix.o");
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/local/src", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("-fcolor-diagnostics");
  compiler_info_flags.push_back("-pthread");
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ(devtools_goma::GCCFlags::LINK, gcc_flags->mode());
  EXPECT_EQ("", gcc_flags->isysroot());
  EXPECT_TRUE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_FALSE(gcc_flags->has_pipe());
}


TEST_F(GCCFlagsTest, ChromeASANCompileFlag) {
  std::vector<string> args;
  args.push_back(
      "/usr/src/chrome/src/third_party/asan/asan_clang_Linux/bin/clang++");
  args.push_back("-fcolor-diagnostics");
  args.push_back("-fasan");
  args.push_back("-w");
  args.push_back("-mllvm");
  args.push_back("-asan-blacklist="
                 "/usr/src/chrome/src/third_party/asan/asan_blacklist.txt");
  args.push_back("-DNO_TCMALLOC");
  args.push_back("-Ithird_party/icu/public/common");
  args.push_back("-Werror");
  args.push_back("-pthread");
  args.push_back("-fno-exceptions");
  args.push_back("-Wall");
  args.push_back("-fvisibility=hidden");
  args.push_back("-pipe");
  args.push_back("-fPIC");
  args.push_back("-MMD");
  args.push_back("-MF");
  args.push_back("out/Release/.deps/out/Release/obj.target/base_unittests/"
                 "base/message_loop_unittest.o.d.raw");
  args.push_back("-c");
  args.push_back("-o");
  args.push_back("out/Release/obj.target/base_unittests/"
                 "base/message_loop_unittest.o base/message_loop_unittest.o");
  args.push_back("out/Release/obj.target/base_unittests/"
                 "base/message_loop_unittest.o base/message_loop_unittest.cc");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(2U, flags->output_files().size());
  EXPECT_EQ("out/Release/obj.target/base_unittests/"
            "base/message_loop_unittest.o base/message_loop_unittest.o",
            flags->output_files()[0]);
  EXPECT_EQ("out/Release/.deps/out/Release/obj.target/base_unittests/"
            "base/message_loop_unittest.o.d.raw",
            flags->output_files()[1]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("out/Release/obj.target/base_unittests/"
            "base/message_loop_unittest.o base/message_loop_unittest.cc",
            flags->input_filenames()[0]);
  EXPECT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ("/usr/src/chrome/src/third_party/asan/asan_blacklist.txt",
            flags->optional_input_filenames()[0]);
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/src/chrome/src", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("-fcolor-diagnostics");
  compiler_info_flags.push_back("-fasan");
  compiler_info_flags.push_back("-pthread");
  compiler_info_flags.push_back("-fno-exceptions");
  compiler_info_flags.push_back("-fvisibility=hidden");
  compiler_info_flags.push_back("-fPIC");
  compiler_info_flags.push_back("-mllvm");
  compiler_info_flags.push_back(
      "-asan-blacklist="
      "/usr/src/chrome/src/third_party/asan/asan_blacklist.txt");
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ(devtools_goma::GCCFlags::COMPILE, gcc_flags->mode());
  EXPECT_TRUE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_TRUE(gcc_flags->has_pipe());
  ASSERT_EQ(1, static_cast<int>(gcc_flags->include_dirs().size()));
  EXPECT_EQ("third_party/icu/public/common", gcc_flags->include_dirs()[0]);
  ASSERT_EQ(1U, gcc_flags->non_system_include_dirs().size());
  EXPECT_EQ("third_party/icu/public/common",
            gcc_flags->non_system_include_dirs()[0]);
  ASSERT_EQ(0U, gcc_flags->root_includes().size());
  ASSERT_EQ(0U, gcc_flags->framework_dirs().size());
  ASSERT_EQ(1U, gcc_flags->commandline_macros().size());
  EXPECT_EQ("NO_TCMALLOC", gcc_flags->commandline_macros()[0].first);
  EXPECT_TRUE(gcc_flags->commandline_macros()[0].second);
}

TEST_F(GCCFlagsTest, ChromeTSANCompileFlag) {
  std::vector<string> args;
  args.push_back(
      "/usr/src/chrome/src/third_party/llvm-build/Release+Asserts/bin/clang++");
  args.push_back("-fcolor-diagnostics");
  args.push_back("-MMD");
  args.push_back("-MF");
  args.push_back("obj/base/message_loop/"
                 "base_unittests.message_loop_unittest.o.d");
  args.push_back("-DTHREAD_SANITIZER");
  args.push_back("-I../../third_party/icu/public/common");
  args.push_back("-Werror");
  args.push_back("-pthread");
  args.push_back("-fno-exceptions");
  args.push_back("-Wall");
  args.push_back("-fvisibility=hidden");
  args.push_back("-pipe");
  args.push_back("-fsanitize=thread");
  args.push_back("-fPIC");
  args.push_back("-mllvm");
  args.push_back("-tsan-blacklist="
                 "../../tools/valgrind/tsan_v2/ignores.txt");
  args.push_back("-c");
  args.push_back("../../base/message_loop/message_loop_unittest.cc");
  args.push_back("-o");
  args.push_back("obj/base/message_loop/"
                 "base_unittests.message_loop_unittest.o");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/src/chrome/src/out/Release"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(2U, flags->output_files().size());
  EXPECT_EQ("obj/base/message_loop/base_unittests.message_loop_unittest.o",
            flags->output_files()[0]);
  EXPECT_EQ("obj/base/message_loop/base_unittests.message_loop_unittest.o.d",
            flags->output_files()[1]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("../../base/message_loop/message_loop_unittest.cc",
            flags->input_filenames()[0]);
  EXPECT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ("../../tools/valgrind/tsan_v2/ignores.txt",
            flags->optional_input_filenames()[0]);
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/src/chrome/src/out/Release", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("-fcolor-diagnostics");
  compiler_info_flags.push_back("-pthread");
  compiler_info_flags.push_back("-fno-exceptions");
  compiler_info_flags.push_back("-fvisibility=hidden");
  compiler_info_flags.push_back("-fsanitize=thread");
  compiler_info_flags.push_back("-fPIC");
  compiler_info_flags.push_back("-mllvm");
  compiler_info_flags.push_back(
      "-tsan-blacklist="
      "../../tools/valgrind/tsan_v2/ignores.txt");
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ(devtools_goma::GCCFlags::COMPILE, gcc_flags->mode());
  EXPECT_TRUE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_TRUE(gcc_flags->has_pipe());
  ASSERT_EQ(1, static_cast<int>(gcc_flags->include_dirs().size()));
  EXPECT_EQ("../../third_party/icu/public/common",
            gcc_flags->include_dirs()[0]);
  ASSERT_EQ(1U, gcc_flags->non_system_include_dirs().size());
  EXPECT_EQ("../../third_party/icu/public/common",
            gcc_flags->non_system_include_dirs()[0]);
  ASSERT_EQ(0U, gcc_flags->root_includes().size());
  ASSERT_EQ(0U, gcc_flags->framework_dirs().size());
  ASSERT_EQ(1U, gcc_flags->commandline_macros().size());
  EXPECT_EQ("THREAD_SANITIZER", gcc_flags->commandline_macros()[0].first);
  EXPECT_TRUE(gcc_flags->commandline_macros()[0].second);
}

TEST_F(GCCFlagsTest, ChromeTSANCompileFlagWithSanitizeBlacklist) {
  std::vector<string> args;
  args.push_back(
      "/usr/src/chrome/src/third_party/llvm-build/Release+Asserts/bin/clang++");
  args.push_back("-fcolor-diagnostics");
  args.push_back("-MMD");
  args.push_back("-MF");
  args.push_back("obj/base/message_loop/"
                 "base_unittests.message_loop_unittest.o.d");
  args.push_back("-DTHREAD_SANITIZER");
  args.push_back("-I../../third_party/icu/public/common");
  args.push_back("-Werror");
  args.push_back("-pthread");
  args.push_back("-fno-exceptions");
  args.push_back("-Wall");
  args.push_back("-fvisibility=hidden");
  args.push_back("-pipe");
  args.push_back("-fsanitize=thread");
  args.push_back("-fPIC");
  args.push_back("-fsanitize-blacklist="
                 "../../tools/valgrind/tsan_v2/ignores.txt");
  args.push_back("-c");
  args.push_back("../../base/message_loop/message_loop_unittest.cc");
  args.push_back("-o");
  args.push_back("obj/base/message_loop/"
                 "base_unittests.message_loop_unittest.o");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/src/chrome/src/out/Release"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(2U, flags->output_files().size());
  EXPECT_EQ("obj/base/message_loop/base_unittests.message_loop_unittest.o",
            flags->output_files()[0]);
  EXPECT_EQ("obj/base/message_loop/base_unittests.message_loop_unittest.o.d",
            flags->output_files()[1]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("../../base/message_loop/message_loop_unittest.cc",
            flags->input_filenames()[0]);
  EXPECT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ("../../tools/valgrind/tsan_v2/ignores.txt",
            flags->optional_input_filenames()[0]);
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/src/chrome/src/out/Release", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("-fcolor-diagnostics");
  compiler_info_flags.push_back("-pthread");
  compiler_info_flags.push_back("-fno-exceptions");
  compiler_info_flags.push_back("-fvisibility=hidden");
  compiler_info_flags.push_back("-fsanitize=thread");
  compiler_info_flags.push_back("-fPIC");
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ(devtools_goma::GCCFlags::COMPILE, gcc_flags->mode());
  EXPECT_TRUE(gcc_flags->is_cplusplus());
  EXPECT_FALSE(gcc_flags->has_nostdinc());
  EXPECT_FALSE(gcc_flags->has_no_integrated_as());
  EXPECT_TRUE(gcc_flags->has_pipe());
  ASSERT_EQ(1, static_cast<int>(gcc_flags->include_dirs().size()));
  EXPECT_EQ("../../third_party/icu/public/common",
            gcc_flags->include_dirs()[0]);
  ASSERT_EQ(1U, gcc_flags->non_system_include_dirs().size());
  EXPECT_EQ("../../third_party/icu/public/common",
            gcc_flags->non_system_include_dirs()[0]);
  ASSERT_EQ(0U, gcc_flags->root_includes().size());
  ASSERT_EQ(0U, gcc_flags->framework_dirs().size());
  ASSERT_EQ(1U, gcc_flags->commandline_macros().size());
  EXPECT_EQ("THREAD_SANITIZER", gcc_flags->commandline_macros()[0].first);
  EXPECT_TRUE(gcc_flags->commandline_macros()[0].second);
}

TEST_F(GCCFlagsTest, ChromeMacDylibLink) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-shared");
  args.push_back("-Wl,-search_paths_first");
  args.push_back("-Wl,-dead_strip");
  args.push_back("-compatibility_version");
  args.push_back("1.0.0");
  args.push_back("-current_version");
  args.push_back("111.1.4");
  args.push_back("-mmacosx-version-min=10.5");
  args.push_back("-isysroot");
  args.push_back("/Developer/SDKs/MacOSX10.5.sdk");
  args.push_back("-arch");
  args.push_back("i386");
  args.push_back("-Lout/Release");
  args.push_back("-install_name");
  args.push_back("/usr/lib/libSystem.B.dylib");
  args.push_back("-o");
  args.push_back("out/Release/libclosure_blocks_leopard_compat_stub.dylib");
  args.push_back("out/Release/obj.target/closure_blocks_leopard_compat/"
                 "content/browser/mac/closure_blocks_leopard_compat.o");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("out/Release/libclosure_blocks_leopard_compat_stub.dylib",
            flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("out/Release/obj.target/closure_blocks_leopard_compat/"
            "content/browser/mac/closure_blocks_leopard_compat.o",
            flags->input_filenames()[0]);
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_EQ("/usr/src/chrome/src", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  EXPECT_EQ(devtools_goma::GCCFlags::LINK, gcc_flags->mode());
}

TEST_F(GCCFlagsTest, ChromeMacInstallName) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-shared");
  args.push_back("-framework");
  args.push_back("Cocoa");
  args.push_back("-Wl,-search_paths_first");
  args.push_back("-Wl,-ObjC");
  args.push_back("-Wl,-dead_strip");
  args.push_back("-mmacosx-version-min=10.6");
  args.push_back("-L.");
  args.push_back("-install_name");
  args.push_back("@executable_path/../Frameworks/"
                 "Content Shell Framework.framework/"
                 "Content Shell Framework");
  args.push_back("-o");
  args.push_back("Content Shell Framework.framework/"
                 "Versions/A/Content Shell Framework");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
}

TEST_F(GCCFlagsTest, ChromeMacRpath) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-rpath");
  args.push_back("@executable_path/../../..");
  args.push_back("-o");
  args.push_back("content_shell_helper_app_executable/"
                 "Content Shell Helper");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
}

TEST_F(GCCFlagsTest, ChromeMacLinkerRpath) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-Xlinker");
  args.push_back("-rpath");
  args.push_back("-Xlinker");
  args.push_back("@executable_path/Frameworks");
  args.push_back("-Xlinker");
  args.push_back("-objc_abi_version");
  args.push_back("-Xlinker");
  args.push_back("2");
  args.push_back("-arch");
  args.push_back("x86_64");
  args.push_back("-o");
  args.push_back("obj/base/x64/base_unittests");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
}

TEST_F(GCCFlagsTest, ClangFDebugPrefixMap) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-fdebug-prefix-map=/foo/bar=/baz");
  args.push_back("-fdebug-prefix-map=/a=/b=/c");
  args.push_back("-fdebug-prefix-map=/d=");
  args.push_back("-c");
  args.push_back("hello.cc");

  GCCFlags flags(args, "/usr/src/chrome/src");

  EXPECT_EQ(args, flags.args());
  EXPECT_TRUE(flags.is_successful());

  std::map<string, string> want_fdebug_prefix_map;
  want_fdebug_prefix_map["/foo/bar"] = "/baz";
  want_fdebug_prefix_map["/a"] = "/b=/c";
  want_fdebug_prefix_map["/d"] = "";
  EXPECT_EQ(want_fdebug_prefix_map, flags.fdebug_prefix_map());
  EXPECT_EQ(std::vector<string>(), flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, ClangShouldDetectBrokenFDebugPrefixMap) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-fdebug-prefix-map=/foo");
  args.push_back("-c");
  args.push_back("hello.cc");

  GCCFlags flags(args, "/usr/src/chrome/src");

  EXPECT_EQ(args, flags.args());
  EXPECT_FALSE(flags.is_successful());
}

TEST_F(GCCFlagsTest, ClangShouldUseFirstFDebugPrefixMap) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-fdebug-prefix-map=/foo=/bar");
  args.push_back("-fdebug-prefix-map=/foo=/baz");
  args.push_back("-c");
  args.push_back("hello.cc");

  GCCFlags flags(args, "/usr/src/chrome/src");

  EXPECT_EQ(args, flags.args());
  EXPECT_TRUE(flags.is_successful());

  std::map<string, string> want_fdebug_prefix_map;
  want_fdebug_prefix_map["/foo"] = "/bar";
  EXPECT_EQ(want_fdebug_prefix_map, flags.fdebug_prefix_map());
  EXPECT_EQ(std::vector<string>(), flags.compiler_info_flags());
}

TEST_F(GCCFlagsTest, ClangKnownFlags) {
  // Taken from the real examples.
  std::vector<string> args {
    "clang++", "-c", "foo.cc",
    "-Qunused-arguments",
    "-Waddress",
    "-nodefaultlibs",
    "-pie",
    "-rdynamic",
    "-nostdlib",
    "-nostdlib++",
    "-static",
    "-dA",
  };

  GCCFlags flags(args, "/");
  EXPECT_TRUE(flags.is_successful());

  EXPECT_TRUE(flags.unknown_flags().empty())
      << "unknown flags="
      << flags.unknown_flags();
}

TEST_F(GCCFlagsTest, Precompiling) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("hello.h");
  GCCFlags flags(args, "/");
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_TRUE(flags.is_precompiling_header());
  ASSERT_EQ(1U, flags.output_files().size());
  EXPECT_EQ("hello.h.gch", flags.output_files()[0]);
}

TEST_F(GCCFlagsTest, PreprocessHeader) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-E");
  args.push_back("hello.h");
  GCCFlags flags(args, "/");
  EXPECT_EQ(GCCFlags::PREPROCESS, flags.mode());
  EXPECT_FALSE(flags.is_precompiling_header());
  EXPECT_EQ(0U, flags.output_files().size());
}

TEST_F(GCCFlagsTest, bazel) {
  // excerpt from https://plus.google.com/113459563087243716523/posts/Vu3hiHmfhE4
  const std::vector<string> args {
    "clang",
    "-DCOMPILER_GCC3",
    "-g0",
    "-Os",
    "-g0",
    "-std=gnu++11",
    "-stdlib=libc++",
    "-MD",
    "-MF", "bazel-out/path/to/foo.d",
    "-frandom-seed=bazel-out/path/to/foo.o",
    "-iquote", ".",
    "-iquote", "bazel-out/path/to/include",
    "-isystem", "path/to/include",
    "-isystem", "another/path/to/include",
    "-Ipath/to/include",
    "-no-canonical-prefixes",
    "-pthread",
    "-c",
    "path/to/foo.cc",
    "-o", "path/to/foo.o",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(2U, flags->output_files().size());
  ExpectHasElement(flags->output_files(), "path/to/foo.o");
  ExpectHasElement(flags->output_files(), "bazel-out/path/to/foo.d");
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("path/to/foo.cc", flags->input_filenames()[0]);
  EXPECT_EQ("clang", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  const std::vector<string> compiler_info_flags {
    "-Os",
    "-std=gnu++11",
    "-stdlib=libc++",
    "-frandom-seed=bazel-out/path/to/foo.o",
    "-iquote", ".",
    "-iquote", "bazel-out/path/to/include",
    "-isystem", "path/to/include",
    "-isystem", "another/path/to/include",
    "-no-canonical-prefixes",
    "-pthread",
  };
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
}

TEST_F(GCCFlagsTest, NoCanonicalPrefixes) {
  const std::vector<string> args {
    "clang", "-c", "-no-canonical-prefixes", "path/to/foo.cc",
    "-o", "path/to/foo.o",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  ExpectHasElement(flags->output_files(), "path/to/foo.o");
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("path/to/foo.cc", flags->input_filenames()[0]);
  EXPECT_EQ("clang", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  const std::vector<string> compiler_info_flags {
    "-no-canonical-prefixes",
  };
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
}

// <path> in -fprofile-sample-use=<path> must be considered as input.
// Set the value as optional input.
TEST_F(GCCFlagsTest, FProfileSampleUse) {
  const std::vector<string> args {
    "clang", "-fprofile-sample-use=path/to/prof.prof",
    "-c", "path/to/foo.c",
    "-o", "path/to/foo.o",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());

  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang", flags->compiler_base_name());
  EXPECT_EQ("clang", flags->compiler_name());

  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("path/to/foo.c", flags->input_filenames()[0]);

  EXPECT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ("path/to/prof.prof", flags->optional_input_filenames()[0]);

  EXPECT_EQ(1U, flags->output_files().size());
  ExpectHasElement(flags->output_files(), "path/to/foo.o");

  // -fprofile-sample-use does not affect CompilerInfo key.
  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_TRUE(gcc_flags->compiler_info_flags().empty());
}

TEST_F(GCCFlagsTest, FThinltoIndex) {
  const std::vector<string> args {
    "clang", "-flto=thin", "-O2", "-o", "file.native.o",
    "-x", "ir", "file.o", "-c",
    "-fthinlto-index=./dir/file.o.chrome.thinlto.bc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang", flags->compiler_base_name());
  EXPECT_EQ("clang", flags->compiler_name());

  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("file.o", flags->input_filenames()[0]);

  EXPECT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ("./dir/file.o.chrome.thinlto.bc",
            flags->optional_input_filenames()[0]);

  EXPECT_EQ(1U, flags->output_files().size());
  ExpectHasElement(flags->output_files(), "file.native.o");

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  const std::vector<string> expected_compiler_info_flags {
    "-flto=thin", "-O2", "-x", "ir",
  };
  EXPECT_EQ(expected_compiler_info_flags, gcc_flags->compiler_info_flags());
  EXPECT_EQ("./dir/file.o.chrome.thinlto.bc", gcc_flags->thinlto_index());
}

TEST_F(GCCFlagsTest, FModules) {
  const std::vector<string> args{
      "clang++", "-fmodules", "-c", "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_EQ("clang++", flags->compiler_name());

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);

  devtools_goma::GCCFlags* gcc_flags =
      static_cast<devtools_goma::GCCFlags*>(flags.get());
  EXPECT_TRUE(gcc_flags->has_fmodules());
  EXPECT_TRUE(gcc_flags->has_fimplicit_module_maps());
  EXPECT_EQ("", gcc_flags->clang_module_map_file());
  EXPECT_EQ("", gcc_flags->clang_module_file().first);
  EXPECT_EQ("", gcc_flags->clang_module_file().second);
}

TEST_F(GCCFlagsTest, FNoImplicitModuleMaps) {
  const std::vector<string> args{
      "clang++", "-fmodules", "-fno-implicit-module-maps", "-c", "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_EQ("clang++", flags->compiler_name());

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);

  EXPECT_EQ(0U, flags->optional_input_filenames().size());

  devtools_goma::GCCFlags* gcc_flags =
      static_cast<devtools_goma::GCCFlags*>(flags.get());
  EXPECT_TRUE(gcc_flags->has_fmodules());
  EXPECT_FALSE(gcc_flags->has_fimplicit_module_maps());
  EXPECT_EQ("", gcc_flags->clang_module_map_file());
  EXPECT_EQ("", gcc_flags->clang_module_file().first);
  EXPECT_EQ("", gcc_flags->clang_module_file().second);
}

TEST_F(GCCFlagsTest, FModulesCachePath) {
  const std::vector<string> args{
      "clang++", "-fmodules", "-fmodule-map-file=foo.modulemap", "-c", "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_EQ("clang++", flags->compiler_name());

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);

  EXPECT_EQ(0U, flags->optional_input_filenames().size());

  devtools_goma::GCCFlags* gcc_flags =
      static_cast<devtools_goma::GCCFlags*>(flags.get());
  EXPECT_TRUE(gcc_flags->has_fmodules());
  EXPECT_TRUE(gcc_flags->has_fimplicit_module_maps());
  EXPECT_EQ("foo.modulemap", gcc_flags->clang_module_map_file());
  EXPECT_EQ("", gcc_flags->clang_module_file().first);
  EXPECT_EQ("", gcc_flags->clang_module_file().second);
}

TEST_F(GCCFlagsTest, FModuleFileWithName) {
  const std::vector<string> args {
    "clang++", "-fmodules", "-fmodule-file=foo=foo.pcm",
    "-c", "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_EQ("clang++", flags->compiler_name());

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);

  EXPECT_EQ(0U, flags->optional_input_filenames().size());

  devtools_goma::GCCFlags* gcc_flags =
      static_cast<devtools_goma::GCCFlags*>(flags.get());
  EXPECT_TRUE(gcc_flags->has_fmodules());
  EXPECT_TRUE(gcc_flags->has_fimplicit_module_maps());
  EXPECT_EQ("", gcc_flags->clang_module_map_file());
  EXPECT_EQ("foo", gcc_flags->clang_module_file().first);
  EXPECT_EQ("foo.pcm", gcc_flags->clang_module_file().second);
}

TEST_F(GCCFlagsTest, FModuleFileWithoutName) {
  const std::vector<string> args {
    "clang++", "-fmodules", "-fmodule-file=foo.pcm",
    "-c", "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_EQ("clang++", flags->compiler_name());

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);

  EXPECT_EQ(0U, flags->optional_input_filenames().size());

  devtools_goma::GCCFlags* gcc_flags =
      static_cast<devtools_goma::GCCFlags*>(flags.get());
  EXPECT_TRUE(gcc_flags->has_fmodules());
  EXPECT_TRUE(gcc_flags->has_fimplicit_module_maps());
  EXPECT_EQ("", gcc_flags->clang_module_map_file());
  EXPECT_EQ("", gcc_flags->clang_module_file().first);
  EXPECT_EQ("foo.pcm", gcc_flags->clang_module_file().second);
}

TEST_F(GCCFlagsTest, FModuleFileFModuleMapFile) {
  const std::vector<string> args{
      "clang++",
      "-fmodules",
      "-fmodule-file=foo=foo.pcm",
      "-fmodule-map-file=foo.modulemap",
      "-c",
      "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_EQ("clang++", flags->compiler_name());

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);

  EXPECT_EQ(0U, flags->optional_input_filenames().size());

  devtools_goma::GCCFlags* gcc_flags =
      static_cast<devtools_goma::GCCFlags*>(flags.get());
  EXPECT_TRUE(gcc_flags->has_fmodules());
  EXPECT_TRUE(gcc_flags->has_fimplicit_module_maps());
  EXPECT_EQ("foo.modulemap", gcc_flags->clang_module_map_file());
  EXPECT_EQ("foo", gcc_flags->clang_module_file().first);
  EXPECT_EQ("foo.pcm", gcc_flags->clang_module_file().second);
}

TEST_F(GCCFlagsTest, FModuleFileCornerCase) {
  const std::vector<string> args{
      "clang++", "-fmodules", "-fmodule-file=foo=", "-c", "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(CompilerFlagType::Gcc, flags->type());

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang++", flags->compiler_base_name());
  EXPECT_EQ("clang++", flags->compiler_name());

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foo.cc", flags->input_filenames()[0]);

  EXPECT_EQ(0U, flags->optional_input_filenames().size());

  devtools_goma::GCCFlags* gcc_flags =
      static_cast<devtools_goma::GCCFlags*>(flags.get());
  EXPECT_TRUE(gcc_flags->has_fmodules());
  EXPECT_TRUE(gcc_flags->has_fimplicit_module_maps());
  EXPECT_EQ("", gcc_flags->clang_module_map_file());
  EXPECT_EQ("foo", gcc_flags->clang_module_file().first);
  EXPECT_EQ("", gcc_flags->clang_module_file().second);
}

}  // namespace devtools_goma
