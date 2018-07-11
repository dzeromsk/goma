// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "vc_flags.h"

#include <limits.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>


#include "absl/strings/str_cat.h"
#include "compiler_flags_parser.h"
#include "file.h"
#include "file_dir.h"
#include "file_helper.h"
#include "filesystem.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"
#include "known_warning_options.h"
#include "path.h"
#include "path_resolver.h"
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

class VCFlagsTest : public testing::Test {
 protected:
  string ComposeOutputFilePath(const string& input,
                               const string& output,
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

 protected:
  string tmp_dir_;
};

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
  const std::vector<string> expected_compiler_info_flags{
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
  EXPECT_EQ(CompilerFlagType::Clexe, flags.type());

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
  EXPECT_EQ(CompilerFlagType::Clexe, flags.type());

  const std::vector<string>& output_files = flags.output_files();
  ASSERT_EQ(1, static_cast<int>(output_files.size()));
  EXPECT_EQ("foobar.obj", output_files[0]);
}

TEST_F(VCFlagsTest, AtFile) {
  std::vector<string> args;
  args.push_back("cl.exe");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back(
      "@" + PathResolver::PlatformConvert(at_file, PathResolver::kWin32PathSep,
                                          PathResolver::kPreserveCase));

  // The at_file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  ASSERT_TRUE(WriteStringToFile(
      "/X /c foobar.c /I d:\\usr\\local\\include /I\"d:\\usr\\include\" "
      "/I\"D:/usr/local\" /D FOO /DNODEBUG /O1 /GF /Gm- /EHsc /RTC1 /MTd "
      "/GS /Gy /fp:precise /Zc:wchar_t /Zc:forScope /GR- "
      "/FP\"Debug\\foobar.pch\" /Fa\"Debug\" /Fo\"foobar.obj\" "
      "/Fd\"D:/foobar/Debug/foobar.pdb\" /Gd /TP /analyze- /errorReport:queue",
      at_file));

  flags = CompilerFlagsParser::MustNew(args, "D:\\foobar");
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

  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());

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
  args.push_back(
      "@" + PathResolver::PlatformConvert(at_file, PathResolver::kWin32PathSep,
                                          PathResolver::kPreserveCase));

  // The at_file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
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

  flags = CompilerFlagsParser::MustNew(args, "C:\\goma work");
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

  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());

  VCFlags* vc_flags = static_cast<VCFlags*>(flags.get());
  EXPECT_FALSE(vc_flags->require_mspdbserv());

  const std::vector<string>& output_files = flags->output_files();
  ASSERT_EQ(7U, output_files.size());
  EXPECT_EQ(
      "C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
      "gtest-filepath.obj",
      flags->output_files()[0]);
  EXPECT_EQ(
      "C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
      "gtest-printers.obj",
      flags->output_files()[1]);
  EXPECT_EQ(
      "C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
      "gtest-port.obj",
      flags->output_files()[2]);
  EXPECT_EQ(
      "C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
      "gtest-death-test.obj",
      flags->output_files()[3]);
  EXPECT_EQ(
      "C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
      "gtest-typed-test.obj",
      flags->output_files()[4]);
  EXPECT_EQ(
      "C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
      "gtest.obj",
      flags->output_files()[5]);
  EXPECT_EQ(
      "C:\\goma work\\client\\build\\Release\\obj\\gtest\\"
      "gtest-test-part.obj",
      flags->output_files()[6]);
}

TEST_F(VCFlagsTest, WCAtFile) {
  std::vector<string> args;
  args.push_back("cl.exe");
  const string& at_file = file::JoinPath(tmp_dir_, "at_file");
  args.push_back(
      "@" + PathResolver::PlatformConvert(at_file, PathResolver::kWin32PathSep,
                                          PathResolver::kPreserveCase));

  // The at_file doesn't exist.
  std::unique_ptr<CompilerFlags> flags(CompilerFlagsParser::MustNew(args, "."));
  EXPECT_FALSE(flags->is_successful());

  static const char kCmdLine[] =
      "\xff\xfe/\0X\0 \0/\0c\0 \0f\0o\0o\0b\0a\0r\0.\0c\0";
  const string kWCCmdLine(kCmdLine, sizeof kCmdLine - 1);
  ASSERT_TRUE(WriteStringToFile(kWCCmdLine, at_file));

  flags = CompilerFlagsParser::MustNew(args, "D:\\foobar");
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
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());

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

  EXPECT_EQ(CompilerFlagType::Clexe, flags.type());
}

// For cl.exe, unknown flags are treated as input.
// So nothing will be treated as unknown.
TEST_F(VCFlagsTest, UnknownFlags) {
  const std::vector<string> args{
      "cl", "/c", "hello.c", "/UNKNOWN", "/UNKNOWN2",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.unknown_flags().empty());
}

TEST_F(VCFlagsTest, BreproWithClExe) {
  const std::vector<string> args{
      "cl", "/Brepro", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, BreproWithClangCl) {
  const std::vector<string> args{
      "clang-cl.exe", "/Brepro", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, LastBreproShouldBeUsed) {
  const std::vector<string> args{
      "clang-cl.exe", "/Brepro", "/Brepro-", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, ClangClShouldSupportNoIncrementalLinkerCompatible) {
  const std::vector<string> args{
      "clang-cl.exe", "-mno-incremental-linker-compatible", "/c", "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_TRUE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, ClangClShouldUseNoIncrementalLinkerCompatible) {
  const std::vector<string> args{
      "clang-cl.exe",
      "/Brepro-",
      "/Brepro",
      "-mno-incremental-linker-compatible",
      "-mincremental-linker-compatible",
      "/c",
      "hello.c",
  };
  VCFlags flags(args, "C:\\");

  EXPECT_TRUE(flags.is_successful());
  EXPECT_FALSE(flags.has_Brepro());
}

TEST_F(VCFlagsTest, ClShouldNotSupportNoIncrementalLinkerCompatible) {
  const std::vector<string> args{
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
  EXPECT_EQ(
      "d:\\src\\\\hello.exe",
      ComposeOutputFilePath("src\\main\\hello.c", "\"d:\\src\\\\\"", ".exe"));
  EXPECT_EQ(
      "k:\\output\\vcflags.exe",
      ComposeOutputFilePath("src\\main.cc", "k:\\output\\vcflags.exe", ".exe"));
  EXPECT_EQ("k:\\output\\vcflags.exe",
            ComposeOutputFilePath("src\\main.cc", "\"k:\\output\\vcflags.exe\"",
                                  ".exe"));
}

TEST_F(VCFlagsTest, VCFlags) {
  std::vector<string> args;
  args.push_back("cl");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_EQ("cl", flags->compiler_base_name());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
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

TEST_F(VCFlagsTest, IsImportantEnvVC) {
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
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));

  for (const auto& tc : kTestCases) {
    ASSERT_TRUE(!tc.server_important || tc.client_important);
    EXPECT_EQ(flags->IsClientImportantEnv(tc.env), tc.client_important)
        << tc.env;
    EXPECT_EQ(flags->IsServerImportantEnv(tc.env), tc.server_important)
        << tc.env;
  }
}

TEST_F(VCFlagsTest, ChromeWindowsCompileFlag) {
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
      CompilerFlagsParser::MustNew(args, "d:\\src\\cr9\\src\\chrome"));

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
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
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

TEST_F(VCFlagsTest, SfntlyWindowsCompileFlag) {
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
      CompilerFlagsParser::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));

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
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
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

TEST_F(VCFlagsTest, VCImplicitMacros) {
  std::vector<string> args;

  // Simple C++ file
  args.push_back("cl");
  args.push_back("/nologo");
  args.push_back("/Zc:forScope");
  args.push_back("/c");
  args.push_back("C:\\src\\sfntly\\cpp\\src\\sfntly\\font.cc");
  std::unique_ptr<CompilerFlags> flags1(
      CompilerFlagsParser::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));
  EXPECT_EQ(args, flags1->args());
  EXPECT_EQ("#define __cplusplus\n", flags1->implicit_macros());

  // Simple C file
  args.clear();
  args.push_back("cl");
  args.push_back("/nologo");
  args.push_back("/c");
  args.push_back("C:\\src\\sfntly\\cpp\\src\\sfntly\\font.c");
  std::unique_ptr<CompilerFlags> flags2(
      CompilerFlagsParser::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));
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
      CompilerFlagsParser::MustNew(args, "C:\\src\\sfntly\\cpp\\build"));
  EXPECT_EQ(args, flags3->args());
  string macro = flags3->implicit_macros();
  EXPECT_TRUE(macro.find("__cplusplus") != string::npos);
  EXPECT_TRUE(macro.find("_VC_NODEFAULTLIB") != string::npos);
  EXPECT_TRUE(macro.find("__MSVC_RUNTIME_CHECKS") != string::npos);
  EXPECT_TRUE(macro.find("_NATIVE_WCHAR_T_DEFINED") != string::npos);
  EXPECT_TRUE(macro.find("_WCHAR_T_DEFINED") != string::npos);

  EXPECT_EQ(CompilerFlagType::Clexe, flags3->type());
  VCFlags* vc_flags = static_cast<VCFlags*>(flags3.get());
  EXPECT_TRUE(vc_flags->require_mspdbserv());
}

TEST_F(VCFlagsTest, ClangCl) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());
}

TEST_F(VCFlagsTest, ClangClWithMflag) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-m64");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-m64");
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, ClangClKnownFlags) {
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

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_TRUE(flags->unknown_flags().empty())
      << "unknown flags: " << flags->unknown_flags();
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeMflag) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-m64");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, ClangClWithHyphenFlagsForCompilerInfo) {
  const std::vector<string> args {
    "clang-cl.exe",
    "-fmsc-version=1800",
    "-fms-compatibility-version=18",
    "-std=c11",
    "/c",
    "hello.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(std::vector<string> { "hello.obj" },
            flags->output_files());
  EXPECT_EQ(std::vector<string> { "hello.cc" },
            flags->input_filenames());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  EXPECT_EQ((std::vector<string> { "-fmsc-version=1800",
                                   "-fms-compatibility-version=18",
                                   "-std=c11" }),
            flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, ClangClWithZi) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("/Zi");
  args.push_back("/c");
  args.push_back("hello.cc");

  {
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, "d:\\tmp"));
    EXPECT_EQ(args, flags->args());
    EXPECT_EQ(1U, flags->output_files().size());
    EXPECT_EQ("hello.obj", flags->output_files()[0]);
    EXPECT_EQ(1U, flags->input_filenames().size());
    EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
    EXPECT_TRUE(flags->is_successful());
    EXPECT_EQ("", flags->fail_message());
    EXPECT_EQ("clang-cl", flags->compiler_name());
    EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
    EXPECT_EQ("d:\\tmp", flags->cwd());

    const VCFlags& vc_flags = static_cast<const VCFlags&>(*flags);
    EXPECT_FALSE(vc_flags.require_mspdbserv());
  }

  args[1] = "/ZI";
  {
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, "d:\\tmp"));
    EXPECT_EQ(args, flags->args());
    EXPECT_EQ(1U, flags->output_files().size());
    EXPECT_EQ("hello.obj", flags->output_files()[0]);
    EXPECT_EQ(1U, flags->input_filenames().size());
    EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
    EXPECT_TRUE(flags->is_successful());
    EXPECT_EQ("", flags->fail_message());
    EXPECT_EQ("clang-cl", flags->compiler_name());
    EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
    EXPECT_EQ("d:\\tmp", flags->cwd());

    const VCFlags& vc_flags = static_cast<const VCFlags&>(*flags);
    EXPECT_FALSE(vc_flags.require_mspdbserv());
  }
}

TEST_F(VCFlagsTest, ClangClISystem) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-isystem=c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(1U, flags->compiler_info_flags().size());
  EXPECT_EQ("-isystem=c:\\clang-cl\\include", flags->compiler_info_flags()[0]);
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeISystem) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-isystem=c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());
}

TEST_F(VCFlagsTest, ClangClImsvc) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-imsvcc:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(1U, flags->compiler_info_flags().size());
  EXPECT_EQ("-imsvcc:\\clang-cl\\include", flags->compiler_info_flags()[0]);

  args[1] = "/imsvcc:\\clang-cl\\include";
  flags = CompilerFlagsParser::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(1U, flags->compiler_info_flags().size());
  EXPECT_EQ("/imsvcc:\\clang-cl\\include", flags->compiler_info_flags()[0]);
}

TEST_F(VCFlagsTest, ClangClImsvcWithValueArg) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-imsvc");
  args.push_back("c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(2U, flags->compiler_info_flags().size());
  EXPECT_EQ("-imsvc", flags->compiler_info_flags()[0]);
  EXPECT_EQ("c:\\clang-cl\\include", flags->compiler_info_flags()[1]);

  args[1] = "/imsvc";
  flags = CompilerFlagsParser::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(2U, flags->compiler_info_flags().size());
  EXPECT_EQ("/imsvc", flags->compiler_info_flags()[0]);
  EXPECT_EQ("c:\\clang-cl\\include", flags->compiler_info_flags()[1]);
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeImsvc) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-imsvcc:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());

  args[1] = "/imsvcc:\\clang-cl\\include";
  flags = CompilerFlagsParser::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeImsvcWithValueArg) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-imsvc");
  args.push_back("c:\\clang-cl\\include");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());

  args[1] = "/imsvc";
  flags = CompilerFlagsParser::MustNew(args, "d:\\tmp");
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  ASSERT_EQ(0U, flags->compiler_info_flags().size());
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeClangClOnlyFlags) {
  const std::vector<string> args {
    "cl.exe",
    "-fmsc-version=1800",
    "-fms-compatibility-version=18",
    "-std=c11",
    "/c",
    "hello.cc",
  };

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(std::vector<string> { "hello.obj" },
            flags->output_files());
  EXPECT_EQ(std::vector<string> { "hello.cc" },
            flags->input_filenames());
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  EXPECT_TRUE(flags->compiler_info_flags().empty());
}

TEST_F(VCFlagsTest, ClangClWithResourceDir) {
  std::vector<string> args = {
    "clang-cl.exe",
    "-resource-dir",
    "this\\is\\resource",
    "/c",
    "hello.cc",
  };
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags = {
    "-resource-dir",
    "this\\is\\resource",
  };
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());

  VCFlags* vc_flags = static_cast<VCFlags*>(flags.get());
  EXPECT_EQ("this\\is\\resource", vc_flags->resource_dir());
}

TEST_F(VCFlagsTest, ClExeWithResourceDir) {
  std::vector<string> args {
    "cl.exe",
    "-resource-dir",
    "this\\is\\resource",
    "/c",
    "hello.cc",
  };
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());

  std::vector<string> expected_compiler_info_flags = {
  };
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());

  VCFlags* vc_flags = static_cast<VCFlags*>(flags.get());
  EXPECT_EQ("", vc_flags->resource_dir());
}

TEST_F(VCFlagsTest, ClangClWithFsanitize) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fsanitize=address");
  args.push_back("-fsanitize=thread");
  args.push_back("-fsanitize=memory");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-fsanitize=address");
  expected_compiler_info_flags.push_back("-fsanitize=thread");
  expected_compiler_info_flags.push_back("-fsanitize=memory");
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, ClangClWithFsanitizeBlacklist) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("-fsanitize-blacklist=blacklist2.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
  std::vector<string> expected_optional_input_filenames;
  expected_optional_input_filenames.push_back("blacklist.txt");
  expected_optional_input_filenames.push_back("blacklist2.txt");
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(VCFlagsTest, ClangClWithFsanitizeAndBlacklist) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fsanitize=address");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-fsanitize=address");
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
  std::vector<string> expected_optional_input_filenames;
  expected_optional_input_filenames.push_back("blacklist.txt");
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(VCFlagsTest, ClangClWithFNoSanitizeBlacklist) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-fno-sanitize-blacklist");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_optional_input_filenames;
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeAnyFsanitize) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-fsanitize=address");
  args.push_back("-fsanitize-blacklist=blacklist.txt");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
  std::vector<string> expected_optional_input_filenames;
  EXPECT_EQ(expected_optional_input_filenames,
            flags->optional_input_filenames());
}

TEST_F(VCFlagsTest, ClangClWithMllvm) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-mllvm");
  args.push_back("-regalloc=pbqp");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-mllvm");
  expected_compiler_info_flags.push_back("-regalloc=pbqp");
  EXPECT_EQ(expected_compiler_info_flags,
            flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeMllvm) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-mllvm");
  args.push_back("-regalloc=pbqp");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, ArchShouldBeRecognizedByClAndClangCl) {
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
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags_cl->args());
  EXPECT_EQ(expected_compiler_info_flags, flags_cl->compiler_info_flags());

  // check clang-cl.
  args[0] = "clang-cl.exe";
  std::unique_ptr<CompilerFlags> flags_clang(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags_clang->args());
  EXPECT_EQ(expected_compiler_info_flags,
            flags_clang->compiler_info_flags());
}

TEST_F(VCFlagsTest, ClangClWithXclang) {
  std::vector<string> args;
  args.push_back("clang-cl.exe");
  args.push_back("-Xclang");
  args.push_back("-add-plugin");
  args.push_back("-Xclang");
  args.push_back("find-bad-constructs");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("clang-cl", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  expected_compiler_info_flags.push_back("-Xclang");
  expected_compiler_info_flags.push_back("-add-plugin");
  expected_compiler_info_flags.push_back("-Xclang");
  expected_compiler_info_flags.push_back("find-bad-constructs");
  EXPECT_EQ(expected_compiler_info_flags,
            flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, ClShouldNotRecognizeXclang) {
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-Xclang");
  args.push_back("-add-plugin");
  args.push_back("-Xclang");
  args.push_back("find-bad-constructs");
  args.push_back("/c");
  args.push_back("hello.cc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
  EXPECT_EQ(args, flags->args());
  EXPECT_EQ(1U, flags->output_files().size());
  EXPECT_EQ("hello.obj", flags->output_files()[0]);
  EXPECT_EQ(1U, flags->input_filenames().size());
  EXPECT_EQ("hello.cc", flags->input_filenames()[0]);
  EXPECT_TRUE(flags->is_successful());
  EXPECT_EQ("", flags->fail_message());
  EXPECT_EQ("cl.exe", flags->compiler_name());
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());

  std::vector<string> expected_compiler_info_flags;
  EXPECT_EQ(expected_compiler_info_flags, flags->compiler_info_flags());
}

TEST_F(VCFlagsTest, CrWinClangCompileFlag) {
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

  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "d:\\tmp"));
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
  EXPECT_EQ(CompilerFlagType::Clexe, flags->type());
  EXPECT_EQ("d:\\tmp", flags->cwd());
}

}  // namespace devtools_goma
