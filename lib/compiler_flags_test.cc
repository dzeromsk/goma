// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "compiler_flags.h"

#include <limits.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>


#include "absl/strings/str_cat.h"
#include "file.h"
#include "file_dir.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"
#include "known_warning_options.h"
#include "path.h"
#include "path_resolver.h"
#ifdef _WIN32
# include "config_win.h"
// we'll ignore the warnings:
// warning C4996: 'strdup': The POSIX name for this item is deprecated.
# pragma warning(disable:4996)
#endif  // _WIN32
using File::CreateDir;
using google::GetExistingTempDirectories;
using std::string;
using absl::StrCat;

namespace devtools_goma {


static void ExpectHasElement(const std::vector<string>& v,
                             const string& elem) {
  EXPECT_TRUE(std::find(v.begin(), v.end(), elem) != v.end()) << elem;
}

static void GetOutputFileForHello(const std::vector<string>& opts,
                                  string* output,
                                  GCCFlags::Mode mode) {
  std::vector<string> args;
  args.push_back("gcc");
  std::copy(opts.begin(), opts.end(), back_inserter(args));
  args.push_back("hello.c");

  GCCFlags flags(args, "/");
  if (flags.output_files().size() >= 1) {
    CHECK_EQ(static_cast<int>(flags.output_files().size()), 1);
    *output = flags.output_files().front();
  } else {
    *output = "";
  }
  EXPECT_EQ(mode, flags.mode()) << args;
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
    tmp_dir_ = file::JoinPath(
        tmp_dirs[0], StrCat("compiler_flags_unittest_", pid));

    CreateDir(tmp_dir_, 0777);
  }
  void TearDown() override {
    RecursivelyDelete(tmp_dir_);
  }
  string GetFileNameExtension(const string& filename) {
    return GCCFlags::GetFileNameExtension(filename);
  }
  string GetLanguage(const string& compiler_name,
                     const string& input_filename) {
    return GCCFlags::GetLanguage(compiler_name, input_filename);
  }

  string tmp_dir_;
};

TEST_F(GCCFlagsTest, GetFileNameExtension) {
  EXPECT_EQ("", GetFileNameExtension(""));
  EXPECT_EQ("cpp", GetFileNameExtension("foo.cpp"));
  EXPECT_EQ("c", GetFileNameExtension("foo.cpp.c"));
  EXPECT_EQ("c", GetFileNameExtension("a/b/x.x/foo.c"));
  EXPECT_EQ("", GetFileNameExtension(".cpp"));
}

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

  const std::vector<string> expected_compiler_info_flags {
      "-m32",
      // TODO: This doesn't change include directory actually.
      "-mtune=generic",
      "-isystem", "/usr",
      "-arch", "ppc",
      "-nostdinc++",
      "-nostdlibinc",
      "-b", "i386",
      "-V", "4.0",
      "-specs", "foo.spec",
      "-std", "c99",
      "-target", "arm-linux-androideabi",
      "-x", "c++",
      "-nostdinc",
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
  const std::set<string> expected_output_files {
    "out/foobar.o", "deps/foobar.d", "deps/foobar2.d"
  };
  EXPECT_EQ(expected_output_files,
            std::set<string>(flags.output_files().begin(),
                             flags.output_files().end()));

  EXPECT_TRUE(flags.is_cplusplus());
  EXPECT_TRUE(flags.has_nostdinc());
  EXPECT_FALSE(flags.has_no_integrated_as());
  EXPECT_FALSE(flags.has_pipe());
  EXPECT_EQ("/tmp", flags.isysroot());
  EXPECT_TRUE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_FALSE(flags.is_vc());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
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

  EXPECT_TRUE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
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

  EXPECT_TRUE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
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

  EXPECT_TRUE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
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

  EXPECT_TRUE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
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

  EXPECT_TRUE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
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

  EXPECT_TRUE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
}

TEST_F(GCCFlagsTest, ClangBaseName) {
  std::vector<string> args;
  args.push_back("/usr/src/chromium/src/"
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
  args.push_back("/usr/src/chromium/src/"
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
  std::vector<string> origs {
    "gcc", "-o", "hello.o", "-c", "hello.c",
  };

  {
    GCCFlags flags(origs, "/");
    EXPECT_FALSE(flags.has_wrapper());
  }
  {
    std::vector<string> args(origs);
    args.insert(args.end(), { "-wrapper", "valgrind" });
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.has_wrapper());
  }
}

TEST_F(GCCFlagsTest, GccFplugin) {
  std::vector<string> origs {
    "gcc", "-o", "hello.o", "-c", "helloc",
  };

  {
    GCCFlags flags(origs, "/");
    EXPECT_FALSE(flags.has_fplugin());
  }

  {
    std::vector<string> args(origs);
    args.insert(args.end(), { "-fplugin=foo.so" });
    GCCFlags flags(args, "/");
    EXPECT_TRUE(flags.has_fplugin());
  }
}

TEST_F(GCCFlagsTest, GccUndef) {
  std::vector<string> origs {
    "gcc", "-undef", "-c", "hello.c",
  };

  GCCFlags flags(origs, "/");

  std::vector<string> want_compiler_info_flags {
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
  std::vector<string> args {
    "clang", "-o", "hello.o", "-fprofile-instr-generate", "-c", "hello.c"};
  GCCFlags flags(args, "/");

  std::vector<string> want_compiler_info_flags {"-fprofile-instr-generate"};
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
  args.push_back("/usr/src/chromium/src/tools/clang/scripts/../../../"
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
  ASSERT_TRUE(CompilerFlags::IsPNaClClangCommand(pnacl_command));
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

TEST_F(GCCFlagsTest, Mode) {
  std::vector<string> opts;
  string output;

  opts.push_back("-c");
  GetOutputFileForHello(opts, &output, GCCFlags::COMPILE);
  EXPECT_EQ("hello.o", output);

  opts[0] = "-S";
  GetOutputFileForHello(opts, &output, GCCFlags::COMPILE);
  EXPECT_EQ("hello.s", output);

  opts[0] = "-E";
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  opts[0] = "-M";
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  // opts[0] = "-M";
  opts.push_back("-c");
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  opts[0] = "-E";
  opts[1] = "-c";
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  opts[0] = "-c";
  opts[1] = "-M";
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  opts[0] = "-c";
  opts[1] = "-E";
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  opts[0] = "-S";
  opts[1] = "-M";
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  opts[0] = "-M";
  opts[1] = "-S";
  GetOutputFileForHello(opts, &output, GCCFlags::PREPROCESS);
  EXPECT_EQ("", output);

  opts[0] = "-c";
  opts[1] = "-S";
  GetOutputFileForHello(opts, &output, GCCFlags::COMPILE);
  EXPECT_EQ("hello.s", output);

  opts[0] = "-S";
  opts[1] = "-c";
  GetOutputFileForHello(opts, &output, GCCFlags::COMPILE);
  EXPECT_EQ("hello.s", output);
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
    ASSERT_TRUE(CreateDir(prof_dir, 0777));

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
    ASSERT_TRUE(RecursivelyDelete(prof_dir));
  }

  {
    // prof rel dir case
    std::vector<string> args;
    args.push_back("clang");
    args.push_back("-c");
    args.push_back("foo/hello.c");

    args.push_back("-fprofile-use=foo");

    const string& prof_dir = file::JoinPath(tmp_dir_, "foo");
    ASSERT_TRUE(CreateDir(prof_dir, 0777));
    GCCFlags flags(args, tmp_dir_);

    EXPECT_TRUE(flags.is_successful());
    ASSERT_EQ(1U, flags.optional_input_filenames().size());

    EXPECT_EQ(file::JoinPath(".", "foo" , "default.profdata"),
              flags.optional_input_filenames()[0]);

    ASSERT_TRUE(RecursivelyDelete(prof_dir));
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
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  ASSERT_TRUE(WriteStringToFile(
      "-c -DFOO '-DBAR=\"a b\\c\"' foo.cc", at_file));
  flags = CompilerFlags::MustNew(args, ".");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

  ASSERT_TRUE(WriteStringToFile(
      " -c -DFOO '-DBAR=\"a b\\c\"' \n foo.cc\n", at_file));
  flags = CompilerFlags::MustNew(args, ".");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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
  const std::vector<string> args {
    "g++", "-c", "foo.cc",
    "-Wp,-Dfoo=bar,-Ufoo2", "-Ufoo", "-Dfoo2=bar2",
    "-Ufoo3", "-Wp,-Dfoo3=bar3", "-Wp,-Dfoo4=bar4,-Ufoo4",
    "-Wp,-MD,deps/foobar.d",
    "-Wp,-unknown1,-unknown2",
    "-Wp,-unknown3",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());

  const std::vector<std::pair<string, bool>> expected_macros {
    { "foo", false },
    { "foo2=bar2", true },
    { "foo3", false },
    { "foo=bar",  true },
    { "foo2", false },
    { "foo3=bar3", true },
    { "foo4=bar4", true },
    { "foo4", false },
  };
  EXPECT_EQ(expected_macros, flags.commandline_macros());

  const std::vector<string> expected_output_files {
    "deps/foobar.d",
  };
  EXPECT_EQ(expected_output_files, flags.output_files());

  const std::vector<string> expected_unknown_flags {
    "-Wp,-unknown1",
    "-Wp,-unknown2",
    "-Wp,-unknown3",
  };
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, LinkerFlags) {
  const std::vector<string> args {
    "g++",
    "-Wl,--start-group",
    "-Wl,--end-group",
    "-Wl,--threads",
    "foo.c",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());

  // all -Wl, are treated as unknown for now.
  const std::vector<string> expected_unknown_flags {
    "-Wl,--start-group",
    "-Wl,--end-group",
    "-Wl,--threads",
  };
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, AssemblerFlags) {
  const std::vector<string> args {
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

  const std::vector<string> expected_unknown_flags {
    "-Wa,-unknown1",
    "-Wa,-unknown2",
    "-Wa,-unknown3",
  };
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, MixW) {
  const std::vector<string> args {
    "g++", "-c", "foo.c",
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

  const std::vector<string> expected_unknown_flags {
    "-Wa,-unknown1",
    "-Wa,-unknown2",
    "-Wl,--defsym,STEREO_OUTPUT",
    "-Wl,--defsym",
    "-Wl,FOO",
    "-Wl,-unknown3",
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
  const std::vector<string> args {
    "clang++", "-c", "foo.cc",
    "--system-header-prefix=a",
    "--system-header-prefix", "b",
    "--no-system-header-prefix=c",
  };

  const std::vector<string> expected_input_files {
    "foo.cc",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_EQ(expected_input_files, flags.input_filenames());
}

TEST_F(GCCFlagsTest, DebugFlags) {
  const std::vector<string> args {
    "g++", "-c", "foo.cc",
    "-g", "-g0", "-g1", "-g2", "-g3",
    "-gcolumn-info", "-gdw", "-gdwarf-2", "-gdwarf-3",
    "-ggdb3", "-ggnu-pubnames", "-gline-tables-only", "-gsplit-dwarf",
    "-gunknown",
  };
  const std::vector<string> expected_unknown_flags {
    "-gunknown",
  };

  GCCFlags flags(args, ".");
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ(GCCFlags::COMPILE, flags.mode());
  EXPECT_EQ(expected_unknown_flags, flags.unknown_flags());
}

TEST_F(GCCFlagsTest, UnknownFlags) {
  const std::vector<string> args {
    "g++", "-c", "foo.cc", "-unknown1", "--unknown2",
  };
  const std::vector<string> expected {
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
  // -Walloc-size-larger-than. This needs "=n"
  EXPECT_FALSE(GCCFlags::IsKnownWarningOption("alloc-size-larger-than"));

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

class JavacFlagsTest : public testing::Test {
 public:
  void SetUp() override {
    std::vector<string> tmp_dirs;
    GetExistingTempDirectories(&tmp_dirs);
    CHECK_GT(tmp_dirs.size(), 0);

#ifndef _WIN32
    string pid = std::to_string(getpid());
#else
    string pid = std::to_string(GetCurrentProcessId());
#endif
    tmp_dir_ = file::JoinPath(
        tmp_dirs[0], StrCat("compiler_flags_unittest_", pid));

    CreateDir(tmp_dir_, 0777);
  }

  void TearDown() override {
    RecursivelyDelete(tmp_dir_);
  }

 protected:
  string tmp_dir_;
};

TEST_F(JavacFlagsTest, Basic) {
  std::vector<string> args;
  args.push_back("javac");
  args.push_back("-J-Xmx512M");
  args.push_back("-target");
  args.push_back("1.5");
  args.push_back("-d");
  args.push_back("dst");
  args.push_back("-s");
  args.push_back("src");
  args.push_back("-cp");
  args.push_back("/tmp:a.jar:b.jar");
  args.push_back("-classpath");
  args.push_back("c.jar");
  args.push_back("-bootclasspath");
  args.push_back("boot1.jar:boot2.jar");
  args.push_back("Hello.java");
  args.push_back("World.java");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_TRUE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ("javac", flags->compiler_name());
  ASSERT_EQ(2U, flags->input_filenames().size());
  EXPECT_EQ("Hello.java", flags->input_filenames()[0]);
  EXPECT_EQ("World.java", flags->input_filenames()[1]);
  std::vector<string> expected_jar_files = {
    "boot1.jar",
    "boot2.jar",
    "a.jar",
    "b.jar",
    "c.jar",
  };
  EXPECT_EQ(expected_jar_files, javac_flags->jar_files());
  EXPECT_EQ(0U, flags->output_files().size());
  ASSERT_EQ(2U, flags->output_dirs().size());
  EXPECT_EQ("dst", flags->output_dirs()[0]);
  EXPECT_EQ("src", flags->output_dirs()[1]);
}

TEST_F(JavacFlagsTest, AtFile) {
  std::vector<string> args;
  args.push_back("javac");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back("@" + at_file);

  // The at-file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  ASSERT_TRUE(
      WriteStringToFile("Hello.java World.java\r\n\t-d dst\r\n-s src",
                        at_file));
  flags = CompilerFlags::MustNew(args, ".");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_TRUE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("javac", flags->compiler_name());
  EXPECT_EQ(7U, flags->expanded_args().size());
  EXPECT_EQ("javac", flags->expanded_args()[0]);
  EXPECT_EQ("Hello.java", flags->expanded_args()[1]);
  EXPECT_EQ("World.java", flags->expanded_args()[2]);
  EXPECT_EQ("-d", flags->expanded_args()[3]);
  EXPECT_EQ("dst", flags->expanded_args()[4]);
  EXPECT_EQ("-s", flags->expanded_args()[5]);
  EXPECT_EQ("src", flags->expanded_args()[6]);
  ASSERT_EQ(2U, flags->input_filenames().size());
  EXPECT_EQ("Hello.java", flags->input_filenames()[0]);
  EXPECT_EQ("World.java", flags->input_filenames()[1]);
  ASSERT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ(PathResolver::PlatformConvert(at_file),
            flags->optional_input_filenames()[0]);
  EXPECT_EQ(0U, flags->output_files().size());
  ASSERT_EQ(2U, flags->output_dirs().size());
  EXPECT_EQ("dst", flags->output_dirs()[0]);
  EXPECT_EQ("src", flags->output_dirs()[1]);
}

TEST_F(JavacFlagsTest, NoDestination) {
  std::vector<string> args;
  args.push_back("javac");
  args.push_back("Hello.java");
  args.push_back("World.java");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_TRUE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("javac", flags->compiler_name());
  ASSERT_EQ(2U, flags->input_filenames().size());
  EXPECT_EQ("Hello.java", flags->input_filenames()[0]);
  EXPECT_EQ("World.java", flags->input_filenames()[1]);
  ASSERT_EQ(2U, flags->output_files().size());
  EXPECT_EQ("Hello.class", flags->output_files()[0]);
  EXPECT_EQ("World.class", flags->output_files()[1]);
}

TEST_F(JavacFlagsTest, Processor) {
  const std::vector<string> args {
    "javac", "-processorpath", "classes.jar",
    "-processor", "dagger.internal.codegen.ComponentProcessor",
    "All.java"
  };
  const std::vector<string> expected_processors {
    "dagger.internal.codegen.ComponentProcessor",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_TRUE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ(expected_processors, javac_flags->processors());
}

TEST_F(JavacFlagsTest, MultipleProcessorArgs) {
  const std::vector<string> args {
    "javac", "-processorpath", "classes.jar",
    "-processor", "dagger.internal.codegen.ComponentProcessor",
    "-processor", "com.google.auto.value.processor.AutoValueProcessor",
    "All.java"
  };
  const std::vector<string> expected_processors {
    "dagger.internal.codegen.ComponentProcessor",
    "com.google.auto.value.processor.AutoValueProcessor",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_TRUE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ(expected_processors, javac_flags->processors());
}

TEST_F(JavacFlagsTest, MultipleProcessorsInArg) {
  const std::vector<string> args {
    "javac", "-processorpath", "classes.jar",
    "-processor",
    "dagger.internal.codegen.ComponentProcessor,"
        "com.google.auto.value.processor.AutoValueProcessor",
    "All.java"
  };
  const std::vector<string> expected_processors {
    "dagger.internal.codegen.ComponentProcessor",
    "com.google.auto.value.processor.AutoValueProcessor",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_TRUE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

  JavacFlags* javac_flags = static_cast<JavacFlags*>(flags.get());
  EXPECT_EQ(expected_processors, javac_flags->processors());
}

TEST_F(JavacFlagsTest, ParseJavaClassPaths) {
  std::vector<string> input = {
    "a.jar:b.zip:c.class",
    "d.jar",
    "e",
  };
  std::vector<string> output;
  ParseJavaClassPaths(input, &output);
  std::vector<string> expected = {
    "a.jar", "b.zip", "d.jar",
  };
  EXPECT_EQ(expected, output);
}

TEST_F(JavacFlagsTest, UnknownFlags) {
  const std::vector<string> args {
    "javac", "-unknown1", "--unknown2",
    "All.java"
  };
  const std::vector<string> expected {
    "-unknown1", "--unknown2",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_EQ(expected, flags->unknown_flags());
}

class VCFlagsTest : public testing::Test {
 protected:
  string GetFileNameExtension(const string& filename) {
    return VCFlags::GetFileNameExtension(filename);
  }
  string ComposeOutputFilePath(const string& input, const string& output,
                               const string& ext) {
    return VCFlags::ComposeOutputFilePath(input, output, ext);
  }

  void SetUp() override {
    std::vector<string> tmp_dirs;
    GetExistingTempDirectories(&tmp_dirs);
    CHECK_GT(tmp_dirs.size(), 0);

#ifndef _WIN32
    string pid = std::to_string(getpid());
#else
    string pid = std::to_string(GetCurrentProcessId());
#endif
    tmp_dir_ = file::JoinPath(
        tmp_dirs[0], StrCat("compiler_flags_unittest_", pid));

    CreateDir(tmp_dir_, 0777);
  }

  void TearDown() override {
    RecursivelyDelete(tmp_dir_);
  }

 protected:
  string tmp_dir_;
};

TEST_F(VCFlagsTest, GetFileNameExtension) {
  EXPECT_EQ("", GetFileNameExtension(""));
  EXPECT_EQ("cpp", GetFileNameExtension("foo.cpp"));
  EXPECT_EQ("c", GetFileNameExtension("foo.cpp.c"));
  EXPECT_EQ("C", GetFileNameExtension("C:\\a\\b\\x.x\\foo.C"));
  EXPECT_EQ("", GetFileNameExtension(".cpp"));
}

TEST_F(VCFlagsTest, Basic) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("/X");
  args.push_back("/c");
  args.push_back("foobar.c");
  args.push_back("/I");
  args.push_back("d:\\usr\\local\\include");
  args.push_back("/I\"d:\\usr\\include\"");
  args.push_back("/I\"D:/usr/local\"");
  args.push_back("/D");
  args.push_back("FOO");
  args.push_back("/DNDEBUG");
  args.push_back("/O1");
  args.push_back("/GF");
  args.push_back("/Gm-");
  args.push_back("/EHsc");
  args.push_back("/RTC1");
  args.push_back("/MTd");
  args.push_back("/GS");
  args.push_back("/Gy");
  args.push_back("/fp:precise");
  args.push_back("/Zc:wchar_t");
  args.push_back("/Zc:forScope");
  args.push_back("/GR-");
  args.push_back("/Fp\"Debug\\foobar.pch\"");
  args.push_back("/Fa\"Debug\"");
  args.push_back("/Fo\"foobar.obj\"");
  args.push_back("/Fd\"D:/foobar/Debug/foobar.pdb\"");
  args.push_back("/Gd");
  args.push_back("/FIpreprocess.h");
  args.push_back("/Yccreate_preprocess.h");
  args.push_back("/Yuuse_preprocess.h");
  args.push_back("/TP");
  args.push_back("/analyze-");
  args.push_back("/errorReport:queue");
  args.push_back("/source-charset:utf-8");
  args.push_back("/execution-charset:utf-8");
  args.push_back("/utf-8");
  args.push_back("/validate-charset");
  args.push_back("/validate-charset-");
  args.push_back("/permissive-");
  args.push_back("/std:c++14");
  args.push_back("/diagnostics:classic,column-");

  VCFlags flags(args, "D:\\foobar");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();

  EXPECT_EQ("cl.exe", flags.compiler_base_name());
  EXPECT_EQ("cl.exe", flags.compiler_name());

  ASSERT_EQ(5, static_cast<int>(flags.compiler_info_flags().size()));
  const std::vector<string> expected_compiler_info_flags {
    "/O1", "/MTd", "/permissive-", "/std:c++14", "/X",
  };
  EXPECT_EQ(expected_compiler_info_flags, flags.compiler_info_flags());

  ASSERT_EQ(1, static_cast<int>(flags.input_filenames().size()));
  EXPECT_EQ("foobar.c", flags.input_filenames()[0]);
  EXPECT_EQ(2U, flags.commandline_macros().size());
  EXPECT_EQ("FOO", flags.commandline_macros()[0].first);
  EXPECT_TRUE(flags.commandline_macros()[0].second);
  EXPECT_EQ("NDEBUG", flags.commandline_macros()[1].first);
  EXPECT_TRUE(flags.commandline_macros()[1].second);
  EXPECT_TRUE(flags.is_cplusplus());
  EXPECT_TRUE(flags.ignore_stdinc());
  EXPECT_FALSE(flags.require_mspdbserv());
  EXPECT_FALSE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_TRUE(flags.is_vc());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());

  ASSERT_EQ(1U, flags.root_includes().size());
  EXPECT_EQ("preprocess.h", flags.root_includes()[0]);

  EXPECT_EQ("create_preprocess.h", flags.creating_pch());
  EXPECT_EQ("use_preprocess.h", flags.using_pch());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(1, static_cast<int>(output_files.size()));
  EXPECT_EQ("foobar.obj", output_files[0]);
}

TEST_F(VCFlagsTest, BasicMixedDash) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("/X");
  args.push_back("/c");
  args.push_back("foobar.c");
  args.push_back("-I");
  args.push_back("d:\\usr\\local\\include");
  args.push_back("-I\"d:\\usr\\include\"");
  args.push_back("-I\"D:/usr/local\"");
  args.push_back("-D");
  args.push_back("FOO");
  args.push_back("-DNDEBUG");
  args.push_back("-O1");
  args.push_back("/GF");
  args.push_back("/Gm-");
  args.push_back("/EHsc");
  args.push_back("/RTC1");
  args.push_back("/MTd");
  args.push_back("/GS");
  args.push_back("/Gy");
  args.push_back("/fp:precise");
  args.push_back("/Zc:wchar_t");
  args.push_back("/Zc:forScope");
  args.push_back("/GR-");
  args.push_back("/Fp\"Debug\\foobar.pch\"");
  args.push_back("/Fa\"Debug\"");
  args.push_back("/Fo\"foobar.obj\"");
  args.push_back("/Fd\"D:/foobar/Debug/foobar.pdb\"");
  args.push_back("/Gd");
  args.push_back("/TP");
  args.push_back("/analyze-");
  args.push_back("/errorReport:queue");

  VCFlags flags(args, "D:\\foobar");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();

  EXPECT_EQ("cl.exe", flags.compiler_base_name());
  EXPECT_EQ("cl.exe", flags.compiler_name());

  ASSERT_EQ(3, static_cast<int>(flags.compiler_info_flags().size()));
  EXPECT_EQ("-O1", flags.compiler_info_flags()[0]);
  EXPECT_EQ("/MTd", flags.compiler_info_flags()[1]);
  EXPECT_EQ("/X", flags.compiler_info_flags()[2]);

  ASSERT_EQ(1, static_cast<int>(flags.input_filenames().size()));
  EXPECT_EQ("foobar.c", flags.input_filenames()[0]);
  EXPECT_EQ(2U, flags.commandline_macros().size());
  EXPECT_EQ("FOO", flags.commandline_macros()[0].first);
  EXPECT_TRUE(flags.commandline_macros()[0].second);
  EXPECT_EQ("NDEBUG", flags.commandline_macros()[1].first);
  EXPECT_TRUE(flags.commandline_macros()[1].second);
  EXPECT_TRUE(flags.is_cplusplus());
  EXPECT_TRUE(flags.ignore_stdinc());
  EXPECT_FALSE(flags.require_mspdbserv());
  EXPECT_FALSE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_TRUE(flags.is_vc());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(1, static_cast<int>(output_files.size()));
  EXPECT_EQ("foobar.obj", output_files[0]);
}

TEST_F(VCFlagsTest, AtFile) {
  std::vector<string> args;
  args.push_back("cl.exe");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back("@" + PathResolver::PlatformConvert(
      at_file, PathResolver::kWin32PathSep, PathResolver::kPreserveCase));

  // The at_file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  ASSERT_TRUE(WriteStringToFile(
      "/X /c foobar.c /I d:\\usr\\local\\include /I\"d:\\usr\\include\" "
      "/I\"D:/usr/local\" /D FOO /DNODEBUG /O1 /GF /Gm- /EHsc /RTC1 /MTd "
      "/GS /Gy /fp:precise /Zc:wchar_t /Zc:forScope /GR- "
      "/FP\"Debug\\foobar.pch\" /Fa\"Debug\" /Fo\"foobar.obj\" "
      "/Fd\"D:/foobar/Debug/foobar.pdb\" /Gd /TP /analyze- /errorReport:queue",
      at_file));

  flags = CompilerFlags::MustNew(args, "D:\\foobar");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_TRUE(flags->fail_message().empty()) << flags->fail_message();

  EXPECT_EQ("cl.exe", flags->compiler_base_name());
  EXPECT_EQ("cl.exe", flags->compiler_name());

  ASSERT_EQ(3U, flags->compiler_info_flags().size());
  EXPECT_EQ("/O1", flags->compiler_info_flags()[0]);
  EXPECT_EQ("/MTd", flags->compiler_info_flags()[1]);
  EXPECT_EQ("/X", flags->compiler_info_flags()[2]);

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foobar.c", flags->input_filenames()[0]);
  ASSERT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ(PathResolver::PlatformConvert(at_file),
            flags->optional_input_filenames()[0]);

  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

  VCFlags* vc_flags = static_cast<VCFlags*>(flags.get());
  EXPECT_FALSE(vc_flags->require_mspdbserv());

  const std::vector<string>& output_files = flags->output_files();
  ASSERT_EQ(1U, output_files.size());
  EXPECT_EQ("foobar.obj", output_files[0]);
}

TEST_F(VCFlagsTest, AtFileQuote) {
  std::vector<string> args;
  args.push_back("cl.exe");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back("@" + PathResolver::PlatformConvert(
      at_file, PathResolver::kWin32PathSep, PathResolver::kPreserveCase));

  // The at_file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  ASSERT_TRUE(WriteStringToFile(
      "/c /Fo\"C:\\goma work\\client\\build\\Release\\obj\\gtest\\\\\" "
      "/Fd\"C:\\goma work\\client\\build\\Release\\gtest.pdb\" "
      "/Gd /TP /analyze- /errorReport:prompt "
      "\"gtest\\src\\gtest-filepath.cc\" "
      "\"gtest\\src\\gtest-printers.cc\" "
      "\"gtest\\src\\gtest-port.cc\" "
      "\"gtest\\src\\gtest-death-test.cc\" "
      "\"gtest\\src\\gtest-typed-test.cc\" "
      "gtest\\src\\gtest.cc \"gtest\\src\\gtest-test-part.cc\" /MP",
      at_file));

  flags = CompilerFlags::MustNew(args, "C:\\goma work");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_TRUE(flags->fail_message().empty()) << flags->fail_message();

  EXPECT_EQ("cl.exe", flags->compiler_base_name());
  EXPECT_EQ("cl.exe", flags->compiler_name());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());

  ASSERT_EQ(7U, flags->input_filenames().size());
  EXPECT_EQ("gtest\\src\\gtest-filepath.cc", flags->input_filenames()[0]);
  EXPECT_EQ("gtest\\src\\gtest-printers.cc", flags->input_filenames()[1]);
  EXPECT_EQ("gtest\\src\\gtest-port.cc", flags->input_filenames()[2]);
  EXPECT_EQ("gtest\\src\\gtest-death-test.cc", flags->input_filenames()[3]);
  EXPECT_EQ("gtest\\src\\gtest-typed-test.cc", flags->input_filenames()[4]);
  EXPECT_EQ("gtest\\src\\gtest.cc", flags->input_filenames()[5]);
  EXPECT_EQ("gtest\\src\\gtest-test-part.cc", flags->input_filenames()[6]);
  ASSERT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ(PathResolver::PlatformConvert(at_file),
            flags->optional_input_filenames()[0]);

  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

  VCFlags* vc_flags = static_cast<VCFlags*>(flags.get());
  EXPECT_FALSE(vc_flags->require_mspdbserv());

  const std::vector<string>& output_files = flags->output_files();
  ASSERT_EQ(7U, output_files.size());
  EXPECT_EQ("C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
            "gtest-filepath.obj", flags->output_files()[0]);
  EXPECT_EQ("C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
            "gtest-printers.obj", flags->output_files()[1]);
  EXPECT_EQ("C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
            "gtest-port.obj", flags->output_files()[2]);
  EXPECT_EQ("C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
            "gtest-death-test.obj", flags->output_files()[3]);
  EXPECT_EQ("C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
            "gtest-typed-test.obj", flags->output_files()[4]);
  EXPECT_EQ("C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
            "gtest.obj", flags->output_files()[5]);
  EXPECT_EQ("C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
            "gtest-test-part.obj", flags->output_files()[6]);
}

TEST_F(VCFlagsTest, WCAtFile) {
  std::vector<string> args;
  args.push_back("cl.exe");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back("@" + PathResolver::PlatformConvert(
      at_file, PathResolver::kWin32PathSep, PathResolver::kPreserveCase));

  // The at_file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  static const char kCmdLine[] =
      "\xff\xfe/\0X\0 \0/\0c\0 \0f\0o\0o\0b\0a\0r\0.\0c\0";
  const string kWCCmdLine(kCmdLine, sizeof kCmdLine - 1);
  ASSERT_TRUE(WriteStringToFile(kWCCmdLine, at_file));

  flags = CompilerFlags::MustNew(args, "D:\\foobar");
  EXPECT_TRUE(flags->is_successful());
  EXPECT_TRUE(flags->fail_message().empty()) << flags->fail_message();

  EXPECT_EQ("cl.exe", flags->compiler_base_name());
  EXPECT_EQ("cl.exe", flags->compiler_name());

  ASSERT_EQ(1U, flags->compiler_info_flags().size());
  EXPECT_EQ("/X", flags->compiler_info_flags()[0]);

  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("foobar.c", flags->input_filenames()[0]);
  ASSERT_EQ(1U, flags->optional_input_filenames().size());
  EXPECT_EQ(PathResolver::PlatformConvert(at_file),
            flags->optional_input_filenames()[0]);
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

  VCFlags* vc_flags = static_cast<VCFlags*>(flags.get());
  EXPECT_FALSE(vc_flags->require_mspdbserv());

  const std::vector<string>& output_files = flags->output_files();
  ASSERT_EQ(1U, output_files.size());
  EXPECT_EQ("foobar.obj", output_files[0]);
}

TEST_F(VCFlagsTest, Optimize) {
  std::vector<string> args;
  args.push_back("cl");
  args.push_back("/O1");
  args.push_back("/c");
  args.push_back("hello.c");
  args.push_back("hello2.cc");

  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.fail_message().empty()) << flags.fail_message();

  EXPECT_EQ("cl", flags.compiler_base_name());
  EXPECT_EQ("cl.exe", flags.compiler_name());

  ASSERT_EQ(1, static_cast<int>(flags.compiler_info_flags().size()));
  EXPECT_EQ("/O1", flags.compiler_info_flags()[0]);

  ASSERT_EQ(2, static_cast<int>(flags.input_filenames().size()));
  EXPECT_EQ("hello.c", flags.input_filenames()[0]);
  EXPECT_EQ("hello2.cc", flags.input_filenames()[1]);

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(2, static_cast<int>(output_files.size()));
  EXPECT_EQ("hello.obj", output_files[0]);
  EXPECT_EQ("hello2.obj", output_files[1]);

  EXPECT_FALSE(flags.ignore_stdinc());
  EXPECT_FALSE(flags.require_mspdbserv());

  EXPECT_FALSE(flags.is_gcc());
  EXPECT_FALSE(flags.is_javac());
  EXPECT_TRUE(flags.is_vc());
  EXPECT_FALSE(flags.is_clang_tidy());
  EXPECT_FALSE(flags.is_java());
}

// For cl.exe, unknown flags are treated as input.
// So nothing will be treated as unknown.
TEST_F(VCFlagsTest, UnknownFlags) {
  const std::vector<string> args {
    "cl", "/c", "hello.c", "/UNKNOWN", "/UNKNOWN2",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.unknown_flags().empty());
}

TEST_F(VCFlagsTest, BreproWithClExe) {
  const std::vector<string> args {
    "cl", "/Brepro", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, BreproWithClangCl) {
  const std::vector<string> args {
    "clang-cl.exe", "/Brepro", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, LastBreproShouldBeUsed) {
  const std::vector<string> args {
    "clang-cl.exe", "/Brepro", "/Brepro-", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, ClangClShouldSupportNoIncrementalLinkerCompatible) {
  const std::vector<string> args {
    "clang-cl.exe", "-mno-incremental-linker-compatible", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, ClangClShouldUseNoIncrementalLinkerCompatible) {
  const std::vector<string> args {
    "clang-cl.exe",
        "/Brepro-",
        "/Brepro",
        "-mno-incremental-linker-compatible",
        "-mincremental-linker-compatible",
        "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, ClShouldNotSupportNoIncrementalLinkerCompatible) {
  const std::vector<string> args {
    "cl", "-mno-incremental-linker-compatible", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, ComposeOutputPath) {
  EXPECT_EQ("hello.exe", ComposeOutputFilePath("hello.c", "", ".exe"));
  EXPECT_EQ("d:\\src\\hello.obj",
      ComposeOutputFilePath("hello.c", "d:\\src\\", ".obj"));
  EXPECT_EQ("d:\\src\\hello.obj",
      ComposeOutputFilePath("src\\hello.c", "\"d:\\src\\\"", ".obj"));
  EXPECT_EQ("d:\\src\\\\hello.exe",
      ComposeOutputFilePath("src\\main\\hello.c", "\"d:\\src\\\\\"", ".exe"));
  EXPECT_EQ("k:\\output\\vcflags.exe",
      ComposeOutputFilePath("src\\main.cc", "k:\\output\\vcflags.exe", ".exe"));
  EXPECT_EQ("k:\\output\\vcflags.exe",
      ComposeOutputFilePath("src\\main.cc",
                            "\"k:\\output\\vcflags.exe\"", ".exe"));
}

class JavaFlagsTest : public testing::Test {
 public:
  void SetUp() override {
    std::vector<string> tmp_dirs;
    GetExistingTempDirectories(&tmp_dirs);
    CHECK_GT(tmp_dirs.size(), 0);

#ifndef _WIN32
    string pid = std::to_string(getpid());
#else
    string pid = std::to_string(GetCurrentProcessId());
#endif
    tmp_dir_ = file::JoinPath(
        tmp_dirs[0], StrCat("compiler_flags_unittest_", pid));

    CreateDir(tmp_dir_, 0777);
  }

  void TearDown() override {
    RecursivelyDelete(tmp_dir_);
  }

 protected:
  string tmp_dir_;
};

TEST_F(JavaFlagsTest, Basic) {
  std::vector<string> args = {
    "prebuilts/jdk/jdk8/linux-x86/bin/java",
    "-Djdk.internal.lambda.dumpProxyClasses="
        "JAVA_LIBRARIES/apache-xml_intermediates/desugar_dumped_classes",
    "-jar",
    "out/host/linux-x86/framework/desugar.jar",
    "--classpath_entry",
    "JAVA_LIBRARIES/core-libart_intermediates/classes-header.jar",
    "--classpath_entry",
    "JAVA_LIBRARIES/core-oj_intermediates/classes-header.jar",
    "--min_sdk_version",
    "10000",
    "--allow_empty_bootclasspath",
    "-i",
    "JAVA_LIBRARIES/apache-xml_intermediates/classes.jar",
    "-o",
    "JAVA_LIBRARIES/apache-xml_intermediates/classes-desugar.jar.tmp",
    "-cp","/tmp:a.jar:b.jar",
    "-classpath", "c.jar",
  };
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  EXPECT_TRUE(flags->is_successful());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_TRUE(flags->is_java());
  EXPECT_EQ("java", flags->compiler_name());
  ASSERT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("out/host/linux-x86/framework/desugar.jar",
            flags->input_filenames()[0]);
  EXPECT_EQ(0U, flags->output_files().size());

  JavaFlags* java_flags = static_cast<JavaFlags*>(flags.get());
  std::vector<string> expected_jar_files = {
    "a.jar",
    "b.jar",
    "c.jar",
  };
  EXPECT_EQ(expected_jar_files, java_flags->jar_files());
}

class CompilerFlagsTest : public ::testing::Test {
};

TEST_F(CompilerFlagsTest, CommandClassification) {
  static const int kGCC = 1 << 0;
  static const int kClang = 1 << 1;
  static const int kVC = 1 << 2;
  static const int kClangCl = 1 << 3;
  static const int kJavac = 1 << 4;
  static const int kClangTidy = 1 << 5;

  struct TestCast {
    const char* command;
    int expected;
  } testcases[] = {
    // gcc
    { "gcc", kGCC },
    { "/usr/bin/gcc", kGCC },
    { "x86_64-linux-gnu-gcc", kGCC },
    { "g++", kGCC },
    { "/usr/bin/g++", kGCC },
    { "x86_64-linux-gnu-g++", kGCC },
    { "c++", kGCC },
    { "/usr/bin/c++", kGCC },
    { "cc", kGCC },
    { "/usr/bin/cc", kGCC },
    { "i586-mingw32msvc-cc", kGCC },
    { "g++-4.8", kGCC },
    { "arm-gnueabihf-gcc-4.9", kGCC },
    { "nacl-gcc", kGCC },
    { "i686-nacl-gcc", kGCC },
    { "nacl-gcc.exe", kGCC },
    // clang
    { "clang", kGCC | kClang },
    { "clang.exe", kGCC | kClang },
    { "/usr/local/bin/clang", kGCC | kClang },
    { "clang++", kGCC | kClang },
    { "/usr/local/bin/clang++", kGCC | kClang },
    { "pnacl-clang", kGCC | kClang },
    { "pnacl-clang++", kGCC | kClang },
    { "clang++-3.7", kGCC | kClang },
    { "/usr/local/google/home/jlebar/bin/clang++-3.7", kGCC | kClang },
    { "armv7a-cros-linux-gnueabi-clang++", kGCC | kClang },  // ChromeOS clang
    { "/usr/bin/local/clang-tidy/clang", kGCC | kClang },  // not clang-tidy.
    // clang (negative)
    { "clang-check", 0 },
    { "clang-tblgen", 0 },
    { "clang-format", 0 },
    { "clang-tidy-diff", 0 },  // not clang-tidy, too.
    // cl
    { "cl", kVC },
    { "CL", kVC },
    { "cl.exe", kVC },
    { "CL.EXE", kVC },
    { "cL.eXe", kVC },
    { "Cl.Exe", kVC },
    { "C:\\VS10\\VC\\bin\\cl.exe", kVC },
    { "D:\\Program Files\\Microsoft Visual Studio 10\\VC\\bin\\Cl.Exe", kVC },
    { "D:\\VS9\\cl.exe\\cl.exe", kVC },
    // cl (negative)
    { "D:\\VS9\\cl.exe\\cl.exe.manifest", 0 },
    { "D:\\VS9\\cl.exe\\", 0 },
    { "cl.exe.manifest", 0 },
    // clang-cl
    { "clang-cl", kClangCl },
    { "clang-cl.exe", kClangCl },
    { "CLANG-CL.EXE", kClangCl },
    { "/usr/local/bin/clang-cl", kClangCl },
    { "/usr/local/bin/clang-cl.exe", kClangCl },
    { "C:\\clang-cl", kClangCl },
    { "C:\\clang-cl.exe", kClangCl },
    { "D:\\example\\clang-cl.exe", kClangCl },
    { "D:\\EXAMPLE\\CLANG-CL.EXE", kClangCl },
    // javac
    { "javac", kJavac },
    { "/usr/bin/javac", kJavac },
    // javac (negative)
    { "/usr/bin/javaco/yes", 0 },
    // clang-tidy
    { "clang-tidy", kClangTidy },
    { "/usr/bin/local/clang-tidy", kClangTidy },
    // others
    { "nacl.exe", 0 },
    { "D:\\nacl_sdk\\pepper_18\\toolchain\\win_x86_newlib\\bin\\nacl.exe", 0 },
    { "/usr/lib/gcc/bin/ar", 0 },
  };

  for (const auto& tc : testcases) {
    EXPECT_EQ(devtools_goma::CompilerFlags::IsGCCCommand(tc.command),
              (tc.expected & kGCC) ? true : false)
        << "command = " << tc.command;
    EXPECT_EQ(devtools_goma::CompilerFlags::IsClangCommand(tc.command),
              (tc.expected & kClang) ? true : false)
        << "command = " << tc.command;
    EXPECT_EQ(devtools_goma::CompilerFlags::IsVCCommand(tc.command),
              (tc.expected & kVC) ? true : false)
        << "command = " << tc.command;
    EXPECT_EQ(devtools_goma::CompilerFlags::IsClangClCommand(tc.command),
              (tc.expected & kClangCl) ? true : false)
        << "command = " << tc.command;
    EXPECT_EQ(devtools_goma::CompilerFlags::IsJavacCommand(tc.command),
              (tc.expected & kJavac) ? true : false)
        << "command = " << tc.command;
    EXPECT_EQ(devtools_goma::CompilerFlags::IsClangTidyCommand(tc.command),
              (tc.expected & kClangTidy) ? true : false)
        << "command = " << tc.command;
  }
}

TEST_F(CompilerFlagsTest, GetCompilerName) {
  using devtools_goma::CompilerFlags;
  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("gcc"));
  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("gcc.exe"));
  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("/usr/bin/gcc"));
  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("x86_64-linux-gnu-gcc"));

  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("g++"));
  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("g++.exe"));
  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("/usr/bin/g++"));
  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("x86_64-linux-gnu-g++"));

  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("nacl-gcc"));
  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("nacl-gcc.exe"));
  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("i686-nacl-gcc"));
  EXPECT_EQ("gcc", CompilerFlags::GetCompilerName("i686-nacl-gcc.exe"));
  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("nacl-g++"));
  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("nacl-g++.exe"));
  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("i686-nacl-g++"));
  EXPECT_EQ("g++", CompilerFlags::GetCompilerName("i686-nacl-g++.exe"));
  EXPECT_EQ("", CompilerFlags::GetCompilerName("nacl.exe"));
  EXPECT_EQ("", CompilerFlags::GetCompilerName(
      "D:\\nacl_sdk\\pepper_18\\toolchain\\win_x86_newlib\\bin\\nacl.exe"));

  EXPECT_EQ("clang", CompilerFlags::GetCompilerName("clang"));
  EXPECT_EQ("clang", CompilerFlags::GetCompilerName("clang.exe"));
  EXPECT_EQ("clang", CompilerFlags::GetCompilerName("/usr/local/bin/clang"));
  EXPECT_EQ("clang", CompilerFlags::GetCompilerName("pnacl-clang"));
  EXPECT_EQ("clang", CompilerFlags::GetCompilerName("pnacl-clang.exe"));
  EXPECT_EQ("clang++", CompilerFlags::GetCompilerName("clang++"));
  EXPECT_EQ("clang++", CompilerFlags::GetCompilerName("clang++.exe"));
  EXPECT_EQ("clang++", CompilerFlags::GetCompilerName(
      "/usr/local/bin/clang++"));
  EXPECT_EQ("clang++", CompilerFlags::GetCompilerName("pnacl-clang++"));
  EXPECT_EQ("clang++", CompilerFlags::GetCompilerName("pnacl-clang++.exe"));
  EXPECT_EQ("", CompilerFlags::GetCompilerName("clang-tblgen"));

  EXPECT_EQ("cl.exe", CompilerFlags::GetCompilerName("cl"));
  EXPECT_EQ("cl.exe", CompilerFlags::GetCompilerName("CL"));
  EXPECT_EQ("cl.exe", CompilerFlags::GetCompilerName("cl.exe"));
  EXPECT_EQ("cl.exe", CompilerFlags::GetCompilerName("CL.EXE"));
  EXPECT_EQ("cl.exe", CompilerFlags::GetCompilerName(
      "C:\\VS10\\VC\\bin\\cl.exe"));
  EXPECT_EQ("cl.exe", CompilerFlags::GetCompilerName(
      "D:\\Program Files\\Microsoft Visual Studio 10\\VC\\bin\\Cl.Exe"));
  EXPECT_EQ("cl.exe", CompilerFlags::GetCompilerName(
      "D:\\VS9\\cl.exe\\cl.exe"));
  EXPECT_EQ("", CompilerFlags::GetCompilerName("cl.exe.manifest"));
  EXPECT_EQ("", CompilerFlags::GetCompilerName(
      "D:\\VS9\\cl.exe\\cl.exe.manifest"));
  EXPECT_EQ("", CompilerFlags::GetCompilerName(
      "D:\\VS9\\cl.exe\\"));

  EXPECT_EQ("javac", CompilerFlags::GetCompilerName("javac"));
  EXPECT_EQ("javac", CompilerFlags::GetCompilerName("/usr/bin/javac"));
}

TEST_F(CompilerFlagsTest, GccFlags) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("hello.c");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.o", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.c", flags->input_filenames()[0]);
  EXPECT_EQ("gcc", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("gcc", flags->compiler_name());
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("/tmp", flags->cwd());

  const size_t env_array_length = 10;
  const char** env =
      static_cast<const char**>(malloc(sizeof(const char*) * env_array_length));
  env[0] = strdup("PATH=/usr/bin:/bin");
  env[1] = strdup("SYSROOT=/tmp/1234");
  env[2] = strdup("LIBRARY_PATH=../libsupp");
  env[3] = strdup("CPATH=.:/special/include");
  env[4] = strdup("C_INCLUDE_PATH=.:/special/include");
  env[5] = strdup("CPLUS_INCLUDE_PATH=.:/special/include/c++");
  env[6] = strdup("OBJC_INCLUDE_PATH=./special/include/objc");
  env[7] = strdup("DEPENDENCIES_OUTPUT=foo.d");
  env[8] = strdup("SUNPRO_DEPENDENCIES=foo.d");
  env[9] = nullptr;

  std::vector<string> important_env;
  flags->GetClientImportantEnvs(env, &important_env);

  std::vector<string> expected_env;
  expected_env.push_back("SYSROOT=/tmp/1234");
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

TEST_F(CompilerFlagsTest, ClangImportantEnv) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-c");
  args.push_back("hello.c");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));

  const size_t env_array_length = 9;
  const char** env =
      static_cast<const char**>(malloc(sizeof(const char*) * env_array_length));
  env[0] = strdup("PATH=/usr/bin:/bin");
  env[1] = strdup("SYSROOT=/tmp/1234");
  env[2] = strdup("LIBRARY_PATH=../libsupp");
  env[3] = strdup("CPATH=.:/special/include");
  env[4] = strdup("C_INCLUDE_PATH=.:/special/include");
  env[5] = strdup("MACOSX_DEPLOYMENT_TARGET=10.7");
  env[6] = strdup("SDKROOT=/tmp/path_to_root");
  env[7] = strdup("DEVELOPER_DIR=/tmp/path_to_developer_dir");
  env[8] = nullptr;

  std::vector<string> important_env;
  flags->GetClientImportantEnvs(env, &important_env);

  std::vector<string> expected_env;
  expected_env.push_back("SYSROOT=/tmp/1234");
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

TEST_F(CompilerFlagsTest, IsImportantEnvGCC) {
  const struct {
    const char* env;
    const bool client_important;
    const bool server_important;
  } kTestCases[] {
    { "SYSROOT=/tmp/1234", true, true },
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
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));

  for (const auto& tc : kTestCases) {
    ASSERT_TRUE(!tc.server_important || tc.client_important);
    EXPECT_EQ(flags->IsClientImportantEnv(tc.env), tc.client_important)
        << tc.env;
    EXPECT_EQ(flags->IsServerImportantEnv(tc.env), tc.server_important)
        << tc.env;
  }
}

TEST_F(CompilerFlagsTest, ChromeLinuxCompileFlag) {
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
      CompilerFlags::MustNew(args, "/usr/local/src"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

TEST_F(CompilerFlagsTest, ChromeLinuxLinkFlag) {
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
      CompilerFlags::MustNew(args, "/usr/local/src"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

TEST_F(CompilerFlagsTest, ChromeLinuxClangCompileFlag) {
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
      CompilerFlags::MustNew(args, "/usr/local/src"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

TEST_F(CompilerFlagsTest, ChromeLinuxClangLinkFlag) {
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
      CompilerFlags::MustNew(args, "/usr/local/src"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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


TEST_F(CompilerFlagsTest, ChromeASANCompileFlag) {
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
      CompilerFlags::MustNew(args, "/usr/src/chrome/src"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

TEST_F(CompilerFlagsTest, ChromeTSANCompileFlag) {
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
      CompilerFlags::MustNew(args, "/usr/src/chrome/src/out/Release"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

TEST_F(CompilerFlagsTest, ChromeTSANCompileFlagWithSanitizeBlacklist) {
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
      CompilerFlags::MustNew(args, "/usr/src/chrome/src/out/Release"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

TEST_F(CompilerFlagsTest, ChromeMacDylibLink) {
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
      CompilerFlags::MustNew(args, "/usr/src/chrome/src"));

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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("/usr/src/chrome/src", flags->cwd());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  EXPECT_FALSE(gcc_flags->is_precompiling_header());
  EXPECT_FALSE(gcc_flags->is_stdin_input());
  EXPECT_EQ(devtools_goma::GCCFlags::LINK, gcc_flags->mode());
}

TEST_F(CompilerFlagsTest, ChromeMacInstallName) {
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
      CompilerFlags::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
}

TEST_F(CompilerFlagsTest, ChromeMacRpath) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-rpath");
  args.push_back("@executable_path/../../..");
  args.push_back("-o");
  args.push_back("content_shell_helper_app_executable/"
                 "Content Shell Helper");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlags::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
}

TEST_F(CompilerFlagsTest, ChromeMacLinkerRpath) {
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
      CompilerFlags::MustNew(args, "/usr/src/chrome/src"));

  EXPECT_EQ(args, flags->args());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
}

TEST_F(CompilerFlagsTest, ClangFDebugPrefixMap) {
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

TEST_F(CompilerFlagsTest, ClangShouldDetectBrokenFDebugPrefixMap) {
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-fdebug-prefix-map=/foo");
  args.push_back("-c");
  args.push_back("hello.cc");

  GCCFlags flags(args, "/usr/src/chrome/src");

  EXPECT_EQ(args, flags.args());
  EXPECT_FALSE(flags.is_successful());
}

TEST_F(CompilerFlagsTest, ClangShouldUseFirstFDebugPrefixMap) {
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

TEST_F(CompilerFlagsTest, ClangKnownFlags) {
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

TEST_F(CompilerFlagsTest, Precompiling) {
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

TEST_F(CompilerFlagsTest, PreprocessHeader) {
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-E");
  args.push_back("hello.h");
  GCCFlags flags(args, "/");
  EXPECT_EQ(GCCFlags::PREPROCESS, flags.mode());
  EXPECT_FALSE(flags.is_precompiling_header());
  EXPECT_EQ(0U, flags.output_files().size());
}

TEST_F(CompilerFlagsTest, GetFirstLine) {
  EXPECT_EQ("gcc (Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            GetFirstLine(
                "gcc (Ubuntu 4.4.3-4ubuntu5) 4.4.3\n"
                "Copyright (C) 2009 Free Software Foundation, Inc.\n"));
}

TEST_F(CompilerFlagsTest, NormalizeGccVersion) {
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion(
                "gcc (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion(
                "cc (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion(
                "g++ (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ("(Ubuntu 4.4.3-4ubuntu5) 4.4.3",
            NormalizeGccVersion(
                "c++ (Ubuntu 4.4.3-4ubuntu5) 4.4.3"));
  EXPECT_EQ("(Native Client SDK [438be0db920e3ca7711844c0218a5db37c747c2b]) "
            "4.8.1",
            NormalizeGccVersion(
                "arm-nacl-gcc (Native Client SDK "
                "[438be0db920e3ca7711844c0218a5db37c747c2b]) 4.8.1"));
  EXPECT_EQ("clang version 3.0 (trunk 129729)",
            NormalizeGccVersion(
                "clang version 3.0 (trunk 129729)"));
  EXPECT_EQ("clang++ version 3.0 (trunk 129729)",
            NormalizeGccVersion(
                "clang++ version 3.0 (trunk 129729)"));
}

TEST_F(CompilerFlagsTest, VCFlags) {
  std::vector<string> args;
  args.push_back("cl");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_EQ("cl", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  const size_t kNumEnvs = 9;
  const char** env =
      static_cast<const char**>(malloc(sizeof(const char*) * kNumEnvs));
  env[0] = strdup("PATH=C:\\Windows\\System32;C:\\VS9\\Common7\\Tools");
  env[1] = strdup("VS90COMNTOOLS=C:\\VS9\\Common7\\Tools");
  env[2] = strdup("VSINSTALLDIR=C:\\VS9");
  env[3] = strdup("VCINSTALLDIR=C:\\vs9");
  env[4] = strdup("INCLUDE=C:\\VS9\\VC\\ATLMFC\\INCLUDE;C:\\VS9\\VC\\INCLUDE;"
                  "C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\include;");
  env[5] = strdup("LIB=C:\\VS9\\VC\\ATLMFC\\LIB;C:\\VS9\\VC\\LIB;"
                  "C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\lib;");
  env[6] = strdup("LIBPATH=C:\\Windows\\Microsoft.NET\\Framework\\v3.5;"
                  "C:\\Windows\\Microsoft.NET\\Framework\\v2.0.50727;"
                  "C:\\VS9\\VC\\ATLMFC\\LIB;C:\\VS9\\VC\\LIB");
  env[7] = strdup("WindowsSdkDir=C:\\Program Files\\Microsoft SDKs\\Windows\\"
                  "v7.1\\");
  env[8] = nullptr;

  std::vector<string> important_env;
  flags->GetClientImportantEnvs(env, &important_env);
  EXPECT_EQ(5U, important_env.size()) << important_env;

  for (int i = 0; i < 9; ++i) {
    if (env[i] != nullptr) {
      free(const_cast<char*>(env[i]));
    }
  }
  free(env);

  devtools_goma::VCFlags* vc_flags = static_cast<devtools_goma::VCFlags*>(
      flags.get());
  std::vector<string> compiler_info_flags;
  EXPECT_EQ(compiler_info_flags, vc_flags->compiler_info_flags());
  EXPECT_TRUE(vc_flags->is_cplusplus());
  EXPECT_FALSE(vc_flags->ignore_stdinc());
}

TEST_F(CompilerFlagsTest, IsImportantEnvVC) {
  const struct {
    const char* env;
    const bool client_important;
    const bool server_important;
  } kTestCases[] {
    { "INCLUDE=/tmp/1234", true, true },
    { "LIB=/tmp/1234", true, true },
    { "MSC_CMD_FLAGS=foo", true, true },
    { "VCINSTALLDIR=/tmp/to", true, true },
    { "VSINSTALLDIR=/tmp/to", true, true },
    { "WindowsSdkDir=/tmp/to", true, true },

    { "PATHEXT=.EXE", true, false },
    { "SystemDrive=C:", true, false },
    { "SystemRoot=C:\\Windows", true, false },

    { "LD_PRELOAD=foo.so", false, false },
    { "ld_preload=foo.so", false, false },
  };

  std::vector<string> args {
    "cl", "/c", "hello.cc",
  };
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));

  for (const auto& tc : kTestCases) {
    ASSERT_TRUE(!tc.server_important || tc.client_important);
    EXPECT_EQ(flags->IsClientImportantEnv(tc.env), tc.client_important)
        << tc.env;
    EXPECT_EQ(flags->IsServerImportantEnv(tc.env), tc.server_important)
        << tc.env;
  }
}

TEST_F(CompilerFlagsTest, ChromeWindowsCompileFlag) {
  // The ridiculously long cl parameters
  std::vector<string> args;
  args.push_back("cl");
  args.push_back("/Od");
  args.push_back("/I");
  args.push_back("\"..\\third_party\\WTL\\include\"");
  args.push_back("/I");
  args.push_back("\"..\"");
  args.push_back("/I");
  args.push_back("\"..\\third_party\\khronos\"");
  args.push_back("/I");
  args.push_back(
      "\"..\\build\\Debug\\obj\\global_intermediate\\chrome_version\"");
  args.push_back("/I");
  args.push_back(
      "\"..\\build\\Debug\\obj\\global_intermediate\\installer_util_strings\"");
  args.push_back("/I");
  args.push_back("\"..\\breakpad\\src\"");
  args.push_back("/I");
  args.push_back("\"..\\sandbox\\src\"");
  args.push_back("/I");
  args.push_back("\"..\\build\\Debug\\obj\\global_intermediate\\policy\"");
  args.push_back("/I");
  args.push_back("\"..\\build\\Debug\\obj\\global_intermediate\\protoc_out\"");
  args.push_back("/I");
  args.push_back("\"..\\third_party\\directxsdk\\files\\Include\"");
  args.push_back("/I");
  args.push_back("\"..\\third_party\\platformsdk_win7\\files\\Include\"");
  args.push_back("/I");
  args.push_back("\"C:\\vs08\\\\VC\\atlmfc\\include\"");
  args.push_back("/D");
  args.push_back("\"_DEBUG\"");
  args.push_back("/D");
  args.push_back("\"_WIN32_WINNT=0x0601\"");
  args.push_back("/D");
  args.push_back("\"WIN32\"");
  args.push_back("/D");
  args.push_back("\"_WINDOWS\"");
  args.push_back("/D");
  args.push_back("\"NOMINMAX\"");
  args.push_back("/D");
  args.push_back("\"PSAPI_VERSION=1\"");
  args.push_back("/D");
  args.push_back("\"_CRT_RAND_S\"");
  args.push_back("/D");
  args.push_back("\"CERT_CHAIN_PARA_HAS_EXTRA_FIELDS\"");
  args.push_back("/D");
  args.push_back("\"WIN32_LEAN_AND_MEAN\"");
  args.push_back("/D");
  args.push_back("\"_ATL_NO_OPENGL\"");
  args.push_back("/D");
  args.push_back("\"_HAS_TR1=0\"");
  args.push_back("/D");
  args.push_back("\"_SECURE_ATL\"");
  args.push_back("/D");
  args.push_back("\"CHROMIUM_BUILD\"");
  args.push_back("/D");
  args.push_back("\"COMPONENT_BUILD\"");
  args.push_back("/D");
  args.push_back("\"COMPILE_CONTENT_STATICALLY\"");
  args.push_back("/D");
  args.push_back("\"TOOLKIT_VIEWS=1\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_REMOTING=1\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_P2P_APIS=1\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_CONFIGURATION_POLICY\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_INPUT_SPEECH\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_NOTIFICATIONS\"");
  args.push_back("/D");
  args.push_back("\"NO_TCMALLOC\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_GPU=1\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_EGLIMAGE=1\"");
  args.push_back("/D");
  args.push_back("\"USE_SKIA=1\"");
  args.push_back("/D");
  args.push_back("\"__STD_C\"");
  args.push_back("/D");
  args.push_back("\"_CRT_SECURE_NO_DEPRECATE\"");
  args.push_back("/D");
  args.push_back("\"_SCL_SECURE_NO_DEPRECATE\"");
  args.push_back("/D");
  args.push_back("\"ENABLE_REGISTER_PROTOCOL_HANDLER=1\"");
  args.push_back("/D");
  args.push_back("\"__STDC_FORMAT_MACROS\"");
  args.push_back("/D");
  args.push_back("\"DYNAMIC_ANNOTATIONS_ENABLED=1\"");
  args.push_back("/D");
  args.push_back("\"WTF_USE_DYNAMIC_ANNOTATIONS=1\"");
  args.push_back("/D");
  args.push_back("\"_DEBUG\"");
  args.push_back("/D");
  args.push_back("\"_UNICODE\"");
  args.push_back("/D");
  args.push_back("\"UNICODE\"");
  args.push_back("/FD");
  args.push_back("/EHsc");
  args.push_back("/RTC1");
  args.push_back("/MDd");
  args.push_back("/Gy");
  args.push_back("/GR-");
  args.push_back("/Yu\"precompile.h\"");
  args.push_back("/Fp\"..\\build\\Debug\\obj\\chrome\\chrome.pch\"");
  args.push_back("/Fo\"..\\build\\Debug\\obj\\chrome\\\\\"");
  args.push_back("/Fd\"..\\build\\Debug\\obj\\chrome\\chrome\\vc80.pdb\"");
  args.push_back("/W4");
  args.push_back("/WX");
  args.push_back("/nologo");
  args.push_back("/c");
  args.push_back("/Zi");
  args.push_back("/TP");
  args.push_back("/wd4351");
  args.push_back("/wd4396");
  args.push_back("/wd4503");
  args.push_back("/wd4819");
  args.push_back("/wd4100");
  args.push_back("/wd4121");
  args.push_back("/wd4125");
  args.push_back("/wd4127");
  args.push_back("/wd4130");
  args.push_back("/wd4131");
  args.push_back("/wd4189");
  args.push_back("/wd4201");
  args.push_back("/wd4238");
  args.push_back("/wd4244");
  args.push_back("/wd4245");
  args.push_back("/wd4310");
  args.push_back("/wd4355");
  args.push_back("/wd4428");
  args.push_back("/wd4481");
  args.push_back("/wd4505");
  args.push_back("/wd4510");
  args.push_back("/wd4512");
  args.push_back("/wd4530");
  args.push_back("/wd4610");
  args.push_back("/wd4611");
  args.push_back("/wd4701");
  args.push_back("/wd4702");
  args.push_back("/wd4706");
  args.push_back("/wd4251");
  args.push_back("/FI");
  args.push_back("\"precompile.h\"");
  args.push_back("/errorReport:prompt");
  args.push_back("/MP");
  args.push_back("/we4389");
  args.push_back("app\\chrome_exe_main_win.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlags::MustNew(args, "d:\\src\\cr9\\src\\chrome"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("..\\build\\Debug\\obj\\chrome\\\\chrome_exe_main_win.obj",
            flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("app\\chrome_exe_main_win.cc", flags->input_filenames()[0]);
  EXPECT_EQ("cl", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\src\\cr9\\src\\chrome", flags->cwd());

  devtools_goma::VCFlags* vc_flags = static_cast<devtools_goma::VCFlags*>(
      flags.get());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("/Od");
  compiler_info_flags.push_back("/MDd");
  EXPECT_EQ(compiler_info_flags, vc_flags->compiler_info_flags());
  EXPECT_TRUE(vc_flags->is_cplusplus());
  EXPECT_FALSE(vc_flags->ignore_stdinc());
  EXPECT_TRUE(vc_flags->require_mspdbserv());
  ASSERT_EQ(12, static_cast<int>(vc_flags->include_dirs().size()));
  EXPECT_EQ("..\\third_party\\WTL\\include", vc_flags->include_dirs()[0]);
  EXPECT_EQ("..", vc_flags->include_dirs()[1]);
  EXPECT_EQ("..\\third_party\\khronos", vc_flags->include_dirs()[2]);

  ASSERT_EQ(35U, vc_flags->commandline_macros().size());
}

TEST_F(CompilerFlagsTest, SfntlyWindowsCompileFlag) {
  std::vector<string> args;
  args.push_back("cl");
  args.push_back("/nologo");
  args.push_back("/DWIN32");
  args.push_back("/D_WINDOWS");
  args.push_back("/Zm100");
  args.push_back("/EHsc");
  args.push_back("/Zi");
  args.push_back("/W4");
  args.push_back("/WX");
  args.push_back("/O2");
  args.push_back("/Ob2");
  args.push_back("/Oy");
  args.push_back("/GF");
  args.push_back("/Gm-");
  args.push_back("/GS");
  args.push_back("/Gy");
  args.push_back("/fp:precise");
  args.push_back("/Zc:wchar_t");
  args.push_back("/Zc:forScope");
  args.push_back("/await");
  args.push_back("/constexpr:depth1024");
  args.push_back("/guard:cf");
  args.push_back("/guard:cf-");
  args.push_back("/ZH:SHA_256");
  args.push_back("/GR-");
  args.push_back("/MD");
  args.push_back("/D");
  args.push_back("NDEBUG");
  args.push_back("/IC:\\src\\sfntly\\cpp\\src");
  args.push_back("/IC:\\src\\sfntly\\cpp\\ext\\gtest\\include");
  args.push_back("/IC:\\src\\sfntly\\cpp\\ext\\gtest");
  args.push_back("/IC:\\src\\sfntly\\cpp\\src\\sample");
  args.push_back("/IC:\\src\\sfntly\\cpp\\src\\sample\\subtly");
  args.push_back("/IC:\\src\\sfntly\\cpp\\ext\\icu\\include");
  args.push_back("/DSFNTLY_NO_EXCEPTION");
  args.push_back("/DTIXML_USE_STL");
  args.push_back("/DSFNTLY_EXPERIMENTAL");
  args.push_back("/D_UNICODE");
  args.push_back("/DUNICODE");
  args.push_back("/TP");
  args.push_back("/FoCMakeFiles\\sfntly.dir\\src\\sfntly\\font.cc.obj");
  args.push_back("/FdC:\\src\\sfntly\\cpp\\build\\lib\\sfntly.pdb");
  args.push_back("/c");
  args.push_back("C:\\src\\sfntly\\cpp\\src\\sfntly\\font.cc");

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlags::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));

  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("CMakeFiles\\sfntly.dir\\src\\sfntly\\font.cc.obj",
            flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("C:\\src\\sfntly\\cpp\\src\\sfntly\\font.cc",
            flags->input_filenames()[0]);
  EXPECT_EQ("cl", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("C:\\src\\sfntly\\cpp\\build", flags->cwd());

  devtools_goma::VCFlags* vc_flags = static_cast<devtools_goma::VCFlags*>(
      flags.get());
  std::vector<string> compiler_info_flags;
  compiler_info_flags.push_back("/O2");
  compiler_info_flags.push_back("/Ob2");
  compiler_info_flags.push_back("/Oy");
  compiler_info_flags.push_back("/MD");
  EXPECT_EQ(compiler_info_flags, vc_flags->compiler_info_flags());
  EXPECT_TRUE(vc_flags->is_cplusplus());
  EXPECT_FALSE(vc_flags->ignore_stdinc());
  EXPECT_TRUE(vc_flags->require_mspdbserv());
  ASSERT_EQ(6, static_cast<int>(vc_flags->include_dirs().size()));
  EXPECT_EQ("C:\\src\\sfntly\\cpp\\src", vc_flags->include_dirs()[0]);
  EXPECT_EQ("C:\\src\\sfntly\\cpp\\ext\\gtest\\include",
            vc_flags->include_dirs()[1]);
  EXPECT_EQ("C:\\src\\sfntly\\cpp\\ext\\icu\\include",
            vc_flags->include_dirs()[5]);
  ASSERT_EQ(8U, vc_flags->commandline_macros().size());
}

TEST_F(CompilerFlagsTest, VCImplicitMacros) {
  std::vector<string> args;

  // Simple C++ file
  args.push_back("cl");
  args.push_back("/nologo");
  args.push_back("/Zc:forScope");
  args.push_back("/c");
  args.push_back("C:\\src\\sfntly\\cpp\\src\\sfntly\\font.cc");
  std::unique_ptr<CompilerFlags> flags1(
      CompilerFlags::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));
  EXPECT_EQ(args, flags1->args());
  EXPECT_EQ("#define __cplusplus\n", flags1->implicit_macros());

  // Simple C file
  args.clear();
  args.push_back("cl");
  args.push_back("/nologo");
  args.push_back("/c");
  args.push_back("C:\\src\\sfntly\\cpp\\src\\sfntly\\font.c");
  std::unique_ptr<CompilerFlags> flags2(
      CompilerFlags::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));
  EXPECT_EQ(args, flags2->args());
  EXPECT_EQ(0UL, flags2->implicit_macros().length());

  // Full fledge
  args.clear();
  args.push_back("cl");
  args.push_back("/nologo");
  args.push_back("/D");
  args.push_back("_DEBUG");
  args.push_back("/RTC");
  args.push_back("/MDd");
  args.push_back("/Zc:wchar_t");
  args.push_back("/ZI");
  args.push_back("/c");
  args.push_back("C:\\src\\sfntly\\cpp\\src\\sfntly\\font.cc");
  std::unique_ptr<CompilerFlags> flags3(
      CompilerFlags::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));
  EXPECT_EQ(args, flags3->args());
  string macro = flags3->implicit_macros();
  EXPECT_TRUE(macro.find("__cplusplus") != string::npos);
  EXPECT_TRUE(macro.find("_VC_NODEFAULTLIB") != string::npos);
  EXPECT_TRUE(macro.find("__MSVC_RUNTIME_CHECKS") != string::npos);
  EXPECT_TRUE(macro.find("_NATIVE_WCHAR_T_DEFINED") != string::npos);
  EXPECT_TRUE(macro.find("_WCHAR_T_DEFINED") != string::npos);

  EXPECT_TRUE(flags3->is_vc());
  VCFlags* vc_flags = static_cast<VCFlags*>(flags3.get());
  EXPECT_TRUE(vc_flags->require_mspdbserv());
}

TEST_F(CompilerFlagsTest, ClangCl) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());
}

TEST_F(CompilerFlagsTest, ClangClWithMflag) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-m64");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-m64");
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ClangClKnownFlags) {
  // These -f and -g are known.
  std::vector<string> args {
    "clang-cl", "/c", "hello.cc",
    "-fcolor-diagnostics",
    "-fno-standalone-debug",
    "-fstandalone-debug",
    "-gcolumn-info",
    "-gline-tables-only",
    "--analyze",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_TRUE(flags->is_vc());
  EXPECT_TRUE(flags->unknown_flags().empty())
      << "unknown flags: " << flags->unknown_flags();
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeMflag) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-m64");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ClangClWithHyphenFlagsForCompilerInfo) {
  const std::vector<string> args {
    "clang-cl.exe",
    "-fmsc-version=1800",
    "-fms-compatibility-version=18",
    "-std=c11",
    "/c",
    "hello.cc",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(std::vector<string> { "hello.obj" },
            flags->output_files());
  EXPECT_EQ(std::vector<string> { "hello.cc" },
            flags->input_filenames());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  EXPECT_EQ((std::vector<string> { "-fmsc-version=1800",
                                   "-fms-compatibility-version=18",
                                   "-std=c11" }),
            flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ClangClWithZi) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("/Zi");
  args.push_back("/c");
  args.push_back("hello.cc");

  {
    std::unique_ptr<CompilerFlags> flags(
      CompilerFlags::MustNew(args, "d:\\tmp"));
    EXPECT_EQ(args, flags->args());
    EXPECT_EQ(1U, flags->output_files().size());
    EXPECT_EQ("hello.obj", flags->output_files()[0]);
    EXPECT_EQ(1U, flags->input_filenames().size());
    EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
    EXPECT_TRUE(flags->is_successful());
    EXPECT_EQ("", flags->fail_message());
    EXPECT_EQ("clang-cl", flags->compiler_name());
    EXPECT_FALSE(flags->is_gcc());
    EXPECT_FALSE(flags->is_javac());
    EXPECT_TRUE(flags->is_vc());
    EXPECT_FALSE(flags->is_clang_tidy());
    EXPECT_FALSE(flags->is_java());
    EXPECT_EQ("d:\\tmp", flags->cwd());

    const VCFlags& vc_flags = static_cast<const VCFlags&>(*flags);
    EXPECT_FALSE(vc_flags.require_mspdbserv());
  }

  args[1] = "/ZI";
  {
    std::unique_ptr<CompilerFlags> flags(
      CompilerFlags::MustNew(args, "d:\\tmp"));
    EXPECT_EQ(args, flags->args());
    EXPECT_EQ(1U, flags->output_files().size());
    EXPECT_EQ("hello.obj", flags->output_files()[0]);
    EXPECT_EQ(1U, flags->input_filenames().size());
    EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
    EXPECT_TRUE(flags->is_successful());
    EXPECT_EQ("", flags->fail_message());
    EXPECT_EQ("clang-cl", flags->compiler_name());
    EXPECT_FALSE(flags->is_gcc());
    EXPECT_FALSE(flags->is_javac());
    EXPECT_TRUE(flags->is_vc());
    EXPECT_FALSE(flags->is_clang_tidy());
    EXPECT_FALSE(flags->is_java());
    EXPECT_EQ("d:\\tmp", flags->cwd());

    const VCFlags& vc_flags = static_cast<const VCFlags&>(*flags);
    EXPECT_FALSE(vc_flags.require_mspdbserv());
  }
}

TEST_F(CompilerFlagsTest, ClangClISystem) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-isystem=c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(1U, flags->compiler_info_flags().size());
  EXPECT_EQ("-isystem=c:\\clang-cl\\include", flags->compiler_info_flags()[0]);
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeISystem) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-isystem=c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());
}

TEST_F(CompilerFlagsTest, ClangClImsvc) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-imsvcc:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(1U, flags->compiler_info_flags().size());
  EXPECT_EQ("-imsvcc:\\clang-cl\\include", flags->compiler_info_flags()[0]);

  args[1] = "/imsvcc:\\clang-cl\\include";
  flags = CompilerFlags::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(1U, flags->compiler_info_flags().size());
  EXPECT_EQ("/imsvcc:\\clang-cl\\include", flags->compiler_info_flags()[0]);
}

TEST_F(CompilerFlagsTest, ClangClImsvcWithValueArg) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-imsvc");
  args.push_back("c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(2U, flags->compiler_info_flags().size());
  EXPECT_EQ("-imsvc", flags->compiler_info_flags()[0]);
  EXPECT_EQ("c:\\clang-cl\\include", flags->compiler_info_flags()[1]);

  args[1] = "/imsvc";
  flags = CompilerFlags::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(2U, flags->compiler_info_flags().size());
  EXPECT_EQ("/imsvc", flags->compiler_info_flags()[0]);
  EXPECT_EQ("c:\\clang-cl\\include", flags->compiler_info_flags()[1]);
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeImsvc) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-imsvcc:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());

  args[1] = "/imsvcc:\\clang-cl\\include";
  flags = CompilerFlags::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeImsvcWithValueArg) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-imsvc");
  args.push_back("c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());

  args[1] = "/imsvc";
  flags = CompilerFlags::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeClangClOnlyFlags) {
  const std::vector<string> args {
    "cl.exe",
    "-fmsc-version=1800",
    "-fms-compatibility-version=18",
    "-std=c11",
    "/c",
    "hello.cc",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(std::vector<string> { "hello.obj" },
            flags->output_files());
  EXPECT_EQ(std::vector<string> { "hello.cc" },
            flags->input_filenames());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  EXPECT_TRUE(flags->compiler_info_flags().empty());
}

TEST_F(CompilerFlagsTest, ClangClWithFsanitize) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fsanitize=address");
  args.push_back("-fsanitize=thread");
  args.push_back("-fsanitize=memory");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-fsanitize=address");
  expected_compiler_info_flags.push_back("-fsanitize=thread");
  expected_compiler_info_flags.push_back("-fsanitize=memory");
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ClangClWithFsanitizeBlacklist) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("-fsanitize-blacklist=blacklist2.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
  std::vector<string> expected_optional_input_filenames;
  expected_optional_input_filenames.push_back("blacklist.txt");
  expected_optional_input_filenames.push_back("blacklist2.txt");
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(CompilerFlagsTest, ClangClWithFsanitizeAndBlacklist) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fsanitize=address");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-fsanitize=address");
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
  std::vector<string> expected_optional_input_filenames;
  expected_optional_input_filenames.push_back("blacklist.txt");
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(CompilerFlagsTest, ClangClWithFNoSanitizeBlacklist) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fno-sanitize-blacklist");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_optional_input_filenames;
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeAnyFsanitize) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-fsanitize=address");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
  std::vector<string> expected_optional_input_filenames;
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(CompilerFlagsTest, ClangClWithMllvm) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-mllvm");
  args.push_back("-regalloc=pbqp");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-mllvm");
  expected_compiler_info_flags.push_back("-regalloc=pbqp");
  EXPECT_EQ(expected_compiler_info_flags,
            flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeMllvm) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-mllvm");
  args.push_back("-regalloc=pbqp");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ArchShouldBeRecognizedByClAndClangCl) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("/arch:AVX2");
  args.push_back("/c");
  args.push_back("hello.cc");

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("/arch:AVX2");

  // check cl.exe.
  args[0] = "cl.exe";
  std::unique_ptr<CompilerFlags> flags_cl(
      CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags_cl->args());
  EXPECT_EQ(expected_compiler_info_flags, flags_cl->compiler_info_flags());

  // check clang-cl.
  args[0] = "clang-cl.exe";
  std::unique_ptr<CompilerFlags> flags_clang(
      CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags_clang->args());
  EXPECT_EQ(expected_compiler_info_flags,
            flags_clang->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ClangClWithXclang) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-Xclang");
  args.push_back("-add-plugin");
  args.push_back("-Xclang");
  args.push_back("find-bad-constructs");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-Xclang");
  expected_compiler_info_flags.push_back("-add-plugin");
  expected_compiler_info_flags.push_back("-Xclang");
  expected_compiler_info_flags.push_back("find-bad-constructs");
  EXPECT_EQ(expected_compiler_info_flags,
            flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, ClShouldNotRecognizeXclang) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-Xclang");
  args.push_back("-add-plugin");
  args.push_back("-Xclang");
  args.push_back("find-bad-constructs");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(CompilerFlagsTest, CrWinClangCompileFlag) {
  // b/18742923
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("/FC");
  args.push_back("-DV8_DEPRECATION_WARNINGS");
  args.push_back("-D_WIN32_WINNT=0x0603");
  args.push_back("-DWINVER=0x0603");
  args.push_back("-DWIN32");
  // snip more -D
  args.push_back("-Igen");
  args.push_back("-I..\\..\\third_party\\wtl\\include");
  // snip more -I
  args.push_back("/wd4127");
  // snip more /wd
  args.push_back("/O2");
  args.push_back("/Ob2");
  args.push_back("/GF");
  args.push_back("/Oy-");
  args.push_back("/fp:precise");
  args.push_back("/W3");
  args.push_back("/GR-");
  args.push_back("/Gy");
  args.push_back("/GS");
  args.push_back("/MT");
  args.push_back("-fmsc-version=1800");
  args.push_back("/fallback");
  args.push_back("/FIIntrin.h");
  args.push_back("-Wno-c++11-compat-deprecated-writable-strings");
  // snip more -W
  args.push_back("-fsanitize=address");
  args.push_back("/d2Zi+");
  args.push_back("/d2FastFail");
  args.push_back("/d2cgsummary");
  args.push_back("/Brepro");
  args.push_back("/Brepro-");
  args.push_back("/Zc:inline");
  args.push_back("/Oy-");
  args.push_back("/FS");
  args.push_back("/TP");
  args.push_back("/c");
  args.push_back("/Foobj\\testing\\gtest.multiprocess_func_list.obj");
  args.push_back("/Fdobj\\testing\\gtest.cc.pdb");
  args.push_back("-Qunused-arguments");
  args.push_back("..\\..\\testing\\multiprocess_func_list.cc");

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("obj\\testing\\gtest.multiprocess_func_list.obj",
            flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("..\\..\\testing\\multiprocess_func_list.cc",
            flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_TRUE(flags->is_vc());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("d:\\tmp", flags->cwd());
}

TEST_F(CompilerFlagsTest, ClangTidyFlag) {
  const std::vector<string> args {
    "clang-tidy",
    "-analyze-temporary-drots",
    "-checks=*",
    "-config={}",
    "-dump-config",
    "-enable-check-profile",
    "-explain-config",
    "-export-fixes=ex.yaml",
    "-extra-arg=-std=c++11",
    "-extra-arg-before=-DFOO",
    "-fix",
    "-fix-errors",
    "-header-filter=*",
    "-line-filter=[]",
    "-list-checks",
    "-p=.",
    "-system-headers",
    "-warnings-as-errors=*",
    "foo.cc",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());

  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("ex.yaml", flags->output_files()[0]);

  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ(file::JoinPath("/tmp", "foo.cc"), flags->input_filenames()[0]);

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-tidy", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_vc());
  EXPECT_TRUE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("/tmp", flags->cwd());

  const ClangTidyFlags& clang_tidy_flags =
      static_cast<const ClangTidyFlags&>(*flags);
  EXPECT_EQ(std::vector<string> { "-std=c++11" },
            clang_tidy_flags.extra_arg());
  EXPECT_EQ(std::vector<string> { "-DFOO" },
            clang_tidy_flags.extra_arg_before());
  EXPECT_FALSE(clang_tidy_flags.seen_hyphen_hyphen());
  EXPECT_EQ(std::vector<string> {},
            clang_tidy_flags.args_after_hyphen_hyphen());
}

TEST_F(CompilerFlagsTest, ClangTidyFlagWithClangArgs) {
  const std::vector<string> args {
    "clang-tidy",
    "-analyze-temporary-drots",
    "-checks=*",
    "-config={}",
    "-dump-config",
    "-enable-check-profile",
    "-explain-config",
    "-export-fixes=ex.yaml",
    "-extra-arg=-std=c++11",
    "-extra-arg-before=-DFOO",
    "-fix",
    "-fix-errors",
    "-header-filter=*",
    "-line-filter=[]",
    "-list-checks",
    "-p=.",
    "-system-headers",
    "-warnings-as-errors=*",
    "foo.cc",
    "--",
    "-DBAR",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());

  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("ex.yaml", flags->output_files()[0]);

  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ(file::JoinPath("/tmp", "foo.cc"), flags->input_filenames()[0]);

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-tidy", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_vc());
  EXPECT_TRUE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("/tmp", flags->cwd());

  const ClangTidyFlags& clang_tidy_flags =
      static_cast<const ClangTidyFlags&>(*flags);
  EXPECT_EQ(std::vector<string> { "-std=c++11" },
            clang_tidy_flags.extra_arg());
  EXPECT_EQ(std::vector<string> { "-DFOO" },
            clang_tidy_flags.extra_arg_before());
  EXPECT_TRUE(clang_tidy_flags.seen_hyphen_hyphen());
  EXPECT_EQ(std::vector<string> { "-DBAR" },
            clang_tidy_flags.args_after_hyphen_hyphen());
}

TEST_F(CompilerFlagsTest, ClangTidyFlagWithClangArgsEndingWithHyphenHyphen) {
  const std::vector<string> args {
    "clang-tidy",
    "-analyze-temporary-drots",
    "-checks=*",
    "-config={}",
    "-dump-config",
    "-enable-check-profile",
    "-explain-config",
    "-export-fixes=ex.yaml",
    "-extra-arg=-std=c++11",
    "-extra-arg-before=-DFOO",
    "-fix",
    "-fix-errors",
    "-header-filter=*",
    "-line-filter=[]",
    "-list-checks",
    "-p=.",
    "-system-headers",
    "-warnings-as-errors=*",
    "foo.cc",
    "--",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());

  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("ex.yaml", flags->output_files()[0]);

  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ(file::JoinPath("/tmp", "foo.cc"), flags->input_filenames()[0]);

  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-tidy", flags->compiler_name());
  EXPECT_FALSE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_vc());
  EXPECT_TRUE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
  EXPECT_EQ("/tmp", flags->cwd());

  const ClangTidyFlags& clang_tidy_flags =
      static_cast<const ClangTidyFlags&>(*flags);
  EXPECT_EQ(std::vector<string> { "-std=c++11" },
            clang_tidy_flags.extra_arg());
  EXPECT_EQ(std::vector<string> { "-DFOO" },
            clang_tidy_flags.extra_arg_before());
  EXPECT_TRUE(clang_tidy_flags.seen_hyphen_hyphen());
  EXPECT_TRUE(clang_tidy_flags.args_after_hyphen_hyphen().empty());
}

TEST_F(CompilerFlagsTest, bazel) {
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

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
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
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

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

TEST_F(CompilerFlagsTest, NoCanonicalPrefixes) {
  const std::vector<string> args {
    "clang", "-c", "-no-canonical-prefixes", "path/to/foo.cc",
    "-o", "path/to/foo.o",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  ExpectHasElement(flags->output_files(), "path/to/foo.o");
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("path/to/foo.cc", flags->input_filenames()[0]);
  EXPECT_EQ("clang", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang", flags->compiler_name());
  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());

  devtools_goma::GCCFlags* gcc_flags = static_cast<devtools_goma::GCCFlags*>(
      flags.get());
  const std::vector<string> compiler_info_flags {
    "-no-canonical-prefixes",
  };
  EXPECT_EQ(compiler_info_flags, gcc_flags->compiler_info_flags());
}

// <path> in -fprofile-sample-use=<path> must be considered as input.
// Set the value as optional input.
TEST_F(CompilerFlagsTest, FProfileSampleUse) {
  const std::vector<string> args {
    "clang", "-fprofile-sample-use=path/to/prof.prof",
    "-c", "path/to/foo.c",
    "-o", "path/to/foo.o",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());

  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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

TEST_F(CompilerFlagsTest, FThinltoIndex) {
  const std::vector<string> args {
    "clang", "-flto=thin", "-O2", "-o", "file.native.o",
    "-x", "ir", "file.o", "-c",
    "-fthinlto-index=./dir/file.o.chrome.thinlto.bc",
  };

  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "/tmp"));
  EXPECT_EQ(args, flags->args());

  EXPECT_TRUE(flags->is_gcc());
  EXPECT_FALSE(flags->is_javac());
  EXPECT_FALSE(flags->is_clang_tidy());
  EXPECT_FALSE(flags->is_java());
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
}

}  // namespace devtools_goma
