// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_info.h"

#include <memory>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <google/protobuf/repeated_field.h>
#include <gtest/gtest.h>

#include "basictypes.h"
#include "compiler_flags.h"
#include "mypath.h"
#include "path.h"
#include "subprocess.h"
#include "unittest_util.h"
#include "util.h"

namespace devtools_goma {

class CompilerInfoTest : public testing::Test {
 protected:
  void SetUp() override {
    CheckTempDirectory(GetGomaTmpDir());
  }

  void AppendPredefinedMacros(const string& macro,
                              CompilerInfoData* cid) {
    cid->set_predefined_macros(cid->predefined_macros() + macro);
  }

  int FindValue(const unordered_map<string, int>& map, const string& key) {
    const auto& it = map.find(key);
    if (it == map.end())
      return 0;
    return it->second;
  }

  string TestDir() {
    // This module is in out\Release.
    const std::string parent_dir = file::JoinPath(GetMyDirectory(), "..");
    const std::string top_dir = file::JoinPath(parent_dir, "..");
    return file::JoinPath(top_dir, "test");
  }
};

TEST_F(CompilerInfoTest, SplitGccIncludeOutput) {
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
  EXPECT_TRUE(CompilerInfoBuilder::SplitGccIncludeOutput(
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

TEST_F(CompilerInfoTest, SplitGccIncludeOutputWithCurIncludePath) {
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
  EXPECT_TRUE(CompilerInfoBuilder::SplitGccIncludeOutput(
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


TEST_F(CompilerInfoTest, IsCwdRelative) {
  {
    std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
    cid->add_cxx_system_include_paths("/usr/local/include");
    cid->add_cxx_system_include_paths(
        "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include");
    cid->add_cxx_system_include_paths(
        "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include-fixed");
    cid->add_cxx_system_include_paths("/usr/include");
    cid->set_found(true);
    CompilerInfo info(std::move(cid));
    EXPECT_FALSE(info.IsCwdRelative("/tmp"));
    EXPECT_TRUE(info.IsCwdRelative("/usr"));
  }

  {
    std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
    cid->add_cxx_system_include_paths("/tmp/.");
    cid->add_cxx_system_include_paths("/usr/local/include");
    cid->add_cxx_system_include_paths(
        "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include");
    cid->add_cxx_system_include_paths(
        "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/include-fixed");
    cid->add_cxx_system_include_paths("/usr/include");
    cid->set_found(true);
    CompilerInfo info(std::move(cid));
    EXPECT_TRUE(info.IsCwdRelative("/tmp"));
    EXPECT_FALSE(info.IsCwdRelative("/usr/src"));
  }
}

TEST_F(CompilerInfoTest, IsCwdRelativeWithSubprogramInfo) {
  TmpdirUtil tmpdir("is_cwd_relative");
  tmpdir.CreateEmptyFile("as");

  CompilerInfoData::SubprogramInfo subprog_data;
  CompilerInfoBuilder::SubprogramInfoFromPath(tmpdir.FullPath("as"),
                                              &subprog_data);
  CompilerInfo::SubprogramInfo subprog;
  CompilerInfo::SubprogramInfo::FromData(subprog_data, &subprog);
  std::vector<CompilerInfo::SubprogramInfo> subprogs;
  subprogs.push_back(subprog);

  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->set_found(true);
  cid->add_subprograms()->CopyFrom(subprog_data);

  CompilerInfo info(std::move(cid));
  EXPECT_TRUE(info.IsCwdRelative(tmpdir.tmpdir()));
  EXPECT_FALSE(info.IsCwdRelative("/nonexistent"));
}

TEST_F(CompilerInfoTest, GetJavacVersion) {
  static const char kVersionInfo[] = "javac 1.6.0_43\n";

  string version;
  CompilerInfoBuilder::ParseJavacVersion(kVersionInfo, &version);
  EXPECT_EQ("1.6.0_43", version);
}

TEST_F(CompilerInfoTest, ParseVCOutput) {
  // output of "cl /nologo /Bxvcflags.exe foo.cpp".
  static const char kInputCpp[] =
    "-zm0x20000000 -il "
    "C:\\Users\\a\\AppData\\Local\\Temp\\_CL_3da4ff85 -typedil -f foo.cpp -W 1 "
    "-Ze -D_MSC_EXTENSIONS -Zp8 -ZB64 -D_INTEGRAL_MAX_BITS=64 -Gs -Ot "
    "-Fofoo.obj -pc \\:/ -Fdvc90.pdb -D_MSC_VER=1500 -D_MSC_FULL_VER=150030729 "
    "-D_MSC_BUILD=1 -D_WIN32 -D_M_IX86=600 -D_M_IX86_FP=0 -GS -GR -D_CPPRTTI "
    "-Zc:forScope -Zc:wchar_t -clrNoPureCRT -D_MT "
    "-I C:\\vs08\\VC\\ATLMFC\\INCLUDE -I C:\\vs08\\VC\\INCLUDE "
    "-I \"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\include\" "
    "-I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\"
    "INCLUDE\\\\\" -I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\"
    "VC\\ATLMFC\\INCLUDE\" "
    "-I \"C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include\"";

  // output of "cl /nologo /B1vcflags.exe foo.c".
  static const char kInputC[] =
    "-zm0x20000000 -il "
    "C:\\Users\\a\\AppData\\Local\\Temp\\_CL_212628dc -typedil -f foo.c -W 1 "
    "-Ze -D_MSC_EXTENSIONS -Zp8 -ZB64 -D_INTEGRAL_MAX_BITS=64 -Gs -Ot "
    "-Fofoo.obj -pc \\:/ -Fdvc90.pdb -D_MSC_VER=1500 -D_MSC_FULL_VER=150030729 "
    "-D_MSC_BUILD=1 -D_WIN32 -D_M_IX86=600 -D_M_IX86_FP=0 -GS -clrNoPureCRT "
    "-D_MT -I C:\\vs08\\VC\\ATLMFC\\INCLUDE -I C:\\vs08\\VC\\INCLUDE "
    "-I \"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\include\" "
    "-I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\"
    "INCLUDE\\\\\" -I \"C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\"
    "VC\\ATLMFC\\INCLUDE\" "
    "-I \"C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include\"";

  std::unique_ptr<CompilerInfoData> info_cpp_data(new CompilerInfoData);
  AppendPredefinedMacros("#define __cplusplus\n", info_cpp_data.get());
  {
    std::vector<string> cxx_system_include_paths;
    string predefined_macros(info_cpp_data->predefined_macros());
    EXPECT_TRUE(CompilerInfoBuilder::ParseVCOutputString(
                  kInputCpp, &cxx_system_include_paths, &predefined_macros));
    for (const auto& p : cxx_system_include_paths) {
      info_cpp_data->add_cxx_system_include_paths(p);
    }
    info_cpp_data->set_predefined_macros(predefined_macros);
  }

  CompilerInfo info_cpp(std::move(info_cpp_data));

  std::vector<string> expected_include_paths;
  expected_include_paths.push_back("C:\\vs08\\VC\\ATLMFC\\INCLUDE");
  expected_include_paths.push_back("C:\\vs08\\VC\\INCLUDE");
  expected_include_paths.push_back(
      "C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\include");
  expected_include_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\VC\\"
      "INCLUDE\\");
  expected_include_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft Visual Studio 10.0\\"
      "VC\\ATLMFC\\INCLUDE");
  expected_include_paths.push_back(
      "C:\\Program Files (x86)\\Microsoft SDKs\\Windows\\v7.0A\\include");
  EXPECT_EQ(0U, info_cpp.system_include_paths().size());
  EXPECT_EQ(0U, info_cpp.system_framework_paths().size());
  EXPECT_EQ(6U, info_cpp.cxx_system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info_cpp.cxx_system_include_paths());
  EXPECT_EQ(
      "#define __cplusplus\n"
      "#define _MSC_EXTENSIONS\n"
      "#define _INTEGRAL_MAX_BITS 64\n"
      "#define _MSC_VER 1500\n"
      "#define _MSC_FULL_VER 150030729\n"
      "#define _MSC_BUILD 1\n"
      "#define _WIN32\n"
      "#define _M_IX86 600\n"
      "#define _M_IX86_FP 0\n"
      "#define _CPPRTTI\n"
      "#define _MT\n",
      info_cpp.predefined_macros());

  std::unique_ptr<CompilerInfoData> info_c_data(new CompilerInfoData);
  {
    std::vector<string> system_include_paths;
    string predefined_macros;
    EXPECT_TRUE(CompilerInfoBuilder::ParseVCOutputString(kInputC,
        &system_include_paths, &predefined_macros));
    for (const auto& p : system_include_paths) {
      info_c_data->add_system_include_paths(p);
    }
    info_c_data->set_predefined_macros(predefined_macros);
  }

  CompilerInfo info_c(std::move(info_c_data));
  EXPECT_EQ(6U, info_c.system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info_c.system_include_paths());
  EXPECT_EQ(0U, info_c.system_framework_paths().size());
  EXPECT_EQ(0U, info_c.cxx_system_include_paths().size());
  EXPECT_EQ(
      "#define _MSC_EXTENSIONS\n"
      "#define _INTEGRAL_MAX_BITS 64\n"
      "#define _MSC_VER 1500\n"
      "#define _MSC_FULL_VER 150030729\n"
      "#define _MSC_BUILD 1\n"
      "#define _WIN32\n"
      "#define _M_IX86 600\n"
      "#define _M_IX86_FP 0\n"
      "#define _MT\n",
      info_c.predefined_macros());

  std::unique_ptr<CompilerInfoData> info_data(new CompilerInfoData);
  AppendPredefinedMacros("#define __cplusplus\n", info_data.get());
  {
    std::vector<string> system_include_paths;
    std::vector<string> cxx_system_include_paths;
    string predefined_macros(info_data->predefined_macros());
    EXPECT_TRUE(CompilerInfoBuilder::ParseVCOutputString(kInputCpp,
        &cxx_system_include_paths, &predefined_macros));
    EXPECT_TRUE(CompilerInfoBuilder::ParseVCOutputString(kInputCpp,
        &system_include_paths, nullptr));
    for (const auto& p : cxx_system_include_paths) {
      info_data->add_cxx_system_include_paths(p);
    }
    for (const auto& p : system_include_paths) {
      info_data->add_system_include_paths(p);
    }
    info_data->set_predefined_macros(predefined_macros);
  }
  CompilerInfo info(std::move(info_data));
  EXPECT_EQ(6U, info.system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info.system_include_paths());
  EXPECT_EQ(0U, info.system_framework_paths().size());
  ASSERT_EQ(6U, info.cxx_system_include_paths().size());
  EXPECT_EQ(expected_include_paths, info.cxx_system_include_paths());
  EXPECT_EQ(
      "#define __cplusplus\n"
      "#define _MSC_EXTENSIONS\n"
      "#define _INTEGRAL_MAX_BITS 64\n"
      "#define _MSC_VER 1500\n"
      "#define _MSC_FULL_VER 150030729\n"
      "#define _MSC_BUILD 1\n"
      "#define _WIN32\n"
      "#define _M_IX86 600\n"
      "#define _M_IX86_FP 0\n"
      "#define _CPPRTTI\n"
      "#define _MT\n",
      info.predefined_macros());

  std::vector<string> dummy;
  EXPECT_FALSE(CompilerInfoBuilder::ParseVCOutputString("\"", &dummy, nullptr));
}

TEST_F(CompilerInfoTest, GetVCVersion) {
  static const char kVc2008[] =
    "Microsoft (R) 32-bit C/C++ Optimizing Compiler Version 15.00.30729.01 for "
    "80x86\r\nCopyright (C) Microsoft Corporation.  All rights reserved.\r\n"
    "\r\nusage: cl [ option... ] filename... [ /link linkoption... ]\r\n";

  static const char kVc2010[] =
    "Microsoft (R) 32-bit C/C++ Optimizing Compiler Version 16.00.40219.01 for "
    "80x86\r\nCopyright (C) Microsoft Corporation.  All rights reserved.\r\n"
    "\r\nusage: cl [ option... ] filename... [ /link linkoption... ]\r\n";

  static const char kWinsdk71[] =
    "Microsoft (R) C/C++ Optimizing Compiler Version 16.00.40219.01 for x64\r\n"
    "Copyright (C) Microsoft Corporation.  All rights reserved.\r\n\r\n"
    "cl : Command line error D8003 : missing source filename";

  string version, target;
  EXPECT_TRUE(CompilerInfoBuilder::ParseVCVersion(kVc2008, &version, &target));
  EXPECT_EQ("15.00.30729.01", version);
  EXPECT_EQ("80x86", target);
  version.clear();
  target.clear();
  EXPECT_TRUE(CompilerInfoBuilder::ParseVCVersion(kVc2010, &version, &target));
  EXPECT_EQ("16.00.40219.01", version);
  EXPECT_EQ("80x86", target);
  version.clear();
  target.clear();
  EXPECT_TRUE(CompilerInfoBuilder::ParseVCVersion(
      kWinsdk71, &version, &target));
  EXPECT_EQ("16.00.40219.01", version);
  EXPECT_EQ("x64", target);
  version.clear();
  target.clear();
  EXPECT_FALSE(CompilerInfoBuilder::ParseVCVersion("", &version, &target));
}

TEST_F(CompilerInfoTest, ParseClangTidyVersionTarget)
{
  const char kOutput[] =
    "LLVM (http://llvm.org/):\n"
    "  LLVM version 3.9.0svn\n"
    "  Optimized build.\n"
    "  Default target: x86_64-unknown-linux-gnu\n"
    "  Host CPU: sandybridge\n";

  string version;
  string target;
  CompilerInfoBuilder::ParseClangTidyVersionTarget(kOutput, &version, &target);

  EXPECT_EQ("3.9.0svn", version);
  EXPECT_EQ("x86_64-unknown-linux-gnu", target);
}

TEST_F(CompilerInfoTest, ClangGcc46) {
  // third_party/llvm-build/Release+Asserts/bin/clang++ -x c++ -v
  // -E /dev/null -o /dev/null
  // on gprecise.
  static const char kClangOutput[] =
      "clang version 3.4 (trunk 184830)\n"
      "Target: x86_64-unknown-linux-gnu\n"
      "Thread model: posix\n"
      " \"/home/goma/src/goma/client/third_party/"
      "llvm-build/Release+Asserts/bin/clang\" -cc1 "
      "-triple x86_64-unknown-linux-gnu -E -disable-free "
      "-main-file-name null -mrelocation-model static "
      "-mdisable-fp-elim -fmath-errno -masm-verbose -mconstructor-aliases "
      "-munwind-tables -target-cpu x86-64 -target-linker-version 2.22 "
      "-v -resource-dir /home/goma/src/goma/client/"
      "third_party/llvm-build/Release+Asserts/bin/../"
      "lib/clang/3.4 "
      "-internal-isystem /usr/lib/gcc/x86_64-linux-gnu/4.6/"
      "../../../../include/c++/4.6 "
      "-internal-isystem /usr/lib/gcc/x86_64-linux-gnu/4.6/"
      "../../../../include/c++/4.6/x86_64-linux-gnu "
      "-internal-isystem /usr/lib/gcc/x86_64-linux-gnu/4.6/"
      "../../../../include/c++/4.6/backward "
      "-internal-isystem /usr/lib/gcc/x86_64-linux-gnu/4.6/"
      "../../../../include/x86_64-linux-gnu/c++/4.6 "
      "-internal-isystem /usr/local/include "
      "-internal-isystem /home/goma/src/goma/client/"
      "third_party/llvm-build/Release+Asserts/bin/../"
      "lib/clang/3.4/include "
      "-internal-externc-isystem /usr/include/x86_64-linux-gnu "
      "-internal-externc-isystem /include "
      "-internal-externc-isystem /usr/include "
      "-fdeprecated-macro "
      "-fdebug-compilation-dir /home/goma/src/goma/client "
      "-ferror-limit 19 -fmessage-length 80 -mstackrealign -fobjc-runtime=gcc "
      "-fobjc-default-synthesize-properties -fcxx-exceptions -fexceptions "
      "-fdiagnostics-show-option -fcolor-diagnostics -vectorize-loops "
      "-o /dev/null -x c++ /dev/null\n"
      "clang -cc1 version 3.4 based upon LLVM 3.4svn default "
      "target x86_64-unknown-linux-gnu\n"
      "ignoring nonexistent directory \"/usr/lib/gcc/x86_64-linux-gnu/4.6/"
      "../../../../include/x86_64-linux-gnu/c++/4.6\"\n"
      "ignoring nonexistent directory \"/include\"\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../include/c++/4.6\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../include/c++/4.6/"
      "x86_64-linux-gnu\n"
      " /usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../include/c++/4.6/"
      "backward\n"
      " /usr/local/include\n"
      " /home/goma/src/goma/client/third_party/"
      "llvm-build/Release+Asserts/bin/../lib/clang/3.4/include\n"
      " /usr/include/x86_64-linux-gnu\n"
      " /usr/include\n"
      "End of search list.\n";
  std::vector<string> args;
  EXPECT_TRUE(CompilerInfoBuilder::GetAdditionalFlags(kClangOutput, &args));
  EXPECT_TRUE(args.empty());
}

TEST_F(CompilerInfoTest, ClangGcc47) {
  // third_party/llvm-build/Release+Asserts/bin/clang++ -x c++ -v
  // -E /dev/null -o /dev/null
  // on goma-chromeos
  static const char kClangOutput[] =
      "clang version 3.4 (trunk 184830)\n"
      "Target: x86_64-unknown-linux-gnu\n"
      "Thread model: posix\n"
      " \"/home/chrome-bot/b/build/slave/goma-chromeos/build"
      "/client/third_party/llvm-build/Release+Asserts/bin/clang\" -cc1 "
      "-triple x86_64-unknown-linux-gnu -E -disable-free -main-file-name null "
      "-mrelocation-model static -mdisable-fp-elim -fmath-errno -masm-verbose "
      "-mconstructor-aliases -munwind-tables -fuse-init-array "
      "-target-cpu x86-64 -target-linker-version 2.22 -v -resource-dir "
      "/home/chrome-bot/b/build/slave/goma-chromeos/build"
      "/client/third_party/llvm-build/Release+Asserts/bin/"
      "../lib/clang/3.4 "
      "-internal-isystem /usr/lib64/gcc/x86_64-pc-linux-gnu/4.7.x-google/"
      "include/g++-v4 "
      "-internal-isystem /usr/lib64/gcc/x86_64-pc-linux-gnu/4.7.x-google/"
      "include/g++-v4/x86_64-pc-linux-gnu "
      "-internal-isystem /usr/lib64/gcc/x86_64-pc-linux-gnu/4.7.x-google/"
      "include/g++-v4/backward "
      "-internal-isystem /usr/local/include "
      "-internal-isystem /home/chrome-bot/b/build/slave/goma-chromeos/build/"
      "client/third_party/llvm-build/Release+Asserts/bin/../"
      "lib/clang/3.4/include "
      "-internal-externc-isystem /include "
      "-internal-externc-isystem /usr/include "
      "-fdeprecated-macro -fdebug-compilation-dir /home/chrome-bot"
      "/b/build/slave/goma-chromeos/build/client "
      "-ferror-limit 19 -fmessage-length 0 -mstackrealign "
      "-fobjc-runtime=gcc -fobjc-default-synthesize-properties "
      "-fcxx-exceptions -fexceptions -fdiagnostics-show-option "
      "-vectorize-loops -o /dev/null -x c++ /dev/null\n"
      "clang -cc1 version 3.4 based upon LLVM 3.4svn default "
      "target x86_64-unknown-linux-gnu\n"
      "ignoring nonexistent directory \"/usr/local/include\"\n"
      "ignoring nonexistent directory \"/include\""
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " /usr/lib64/gcc/x86_64-pc-linux-gnu/4.7.x-google/include/g++-v4\n"
      " /usr/lib64/gcc/x86_64-pc-linux-gnu/4.7.x-google/include/g++-v4/"
      "x86_64-pc-linux-gnu\n"
      "/usr/lib64/gcc/x86_64-pc-linux-gnu/4.7.x-google/include/g++-v4/"
      "backward\n"
      " /home/chrome-bot/b/build/slave/goma-chromeos/build/"
      "client/third_party/llvm-build/Release+Asserts/bin/../"
      "lib/clang/3.4/include\n"
      " /usr/include\n"
      "End of search list.\n";
  std::vector<string> args;
  EXPECT_TRUE(CompilerInfoBuilder::GetAdditionalFlags(kClangOutput, &args));
  std::vector<string> expected_args;
  expected_args.push_back("-fuse-init-array");
  EXPECT_EQ(expected_args, args);
}

TEST_F(CompilerInfoTest, ParseFeatures) {
  static const char* kDummyObjectMacros[] = {
      "dummy_macro1",
      "dummy_macro2",
  };
  static const unsigned long kNumDummyObjectMacros =
      arraysize(kDummyObjectMacros);
  static const char* kDummyFunctionMacros[] = {
      "dummy_func1",
      "dummy_func2",
  };
  static const unsigned long kNumDummyFunctionMacros =
      arraysize(kDummyFunctionMacros);
  static const char* kDummyFeatures[] = {
      "dummy_feature1",
      "dummy_feature2",
  };
  static const unsigned long kNumDummyFeatures =
      arraysize(kDummyFeatures);
  static const char* kDummyExtensions[] = {
      "dummy_extension1",
      "dummy_extension2",
  };
  static const unsigned long kNumDummyExtensions =
      arraysize(kDummyExtensions);
  static const char* kDummyAttributes[] = {
      "dummy_attribute1",
      "dummy_attribute2",
      "dummy_attribute3",
      "dummy_attribute4",
      "_Alignas",
      "asm",
  };
  static const unsigned long kNumDummyAttributes =
      arraysize(kDummyAttributes);
  static const char* kDummyCppAttributes[] = {
      "dummy_cpp_attribute1",
      "dummy_cpp_attribute2",
      "clang::dummy_cpp_attribute1",
      "clang::dummy_cpp_attribute2",
  };
  static const unsigned long kNumDummyCppAttributes =
      arraysize(kDummyCppAttributes);

  static const char* kDummyDeclspecAttributes[] = {
      "dummy_declspec_attributes1",
      "dummy_declspec_attributes2",
  };
  static const unsigned long kNumDummyDeclspecAttributes =
     arraysize(kDummyDeclspecAttributes);

  static const char* kDummyBuiltins[] = {
      "dummy_builtin1",
      "dummy_builtin2",
  };
  static const unsigned long kNumDummyBuiltins = arraysize(kDummyBuiltins);

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
      "# 15\n"         // cpp attributes.
      "201304\n"
      "# 16\n"
      "0\n"
      "# 17\n"
      "201301\n"
      "# 18\n"
      "0\n"
      "# 19\n"         // declspec attributes.
      "1\n"
      "# 20\n"
      "0\n"
      "# 21\n"         // builtins
      "1\n"
      "# 22\n"
      "0\n";

  CompilerInfoBuilder::FeatureList object_macros = std::make_pair(
      kDummyObjectMacros, kNumDummyObjectMacros);
  CompilerInfoBuilder::FeatureList function_macros = std::make_pair(
      kDummyFunctionMacros, kNumDummyFunctionMacros);
  CompilerInfoBuilder::FeatureList features = std::make_pair(
      kDummyFeatures, kNumDummyFeatures);
  CompilerInfoBuilder::FeatureList extensions = std::make_pair(
      kDummyExtensions, kNumDummyExtensions);
  CompilerInfoBuilder::FeatureList attributes = std::make_pair(
      kDummyAttributes, kNumDummyAttributes);
  CompilerInfoBuilder::FeatureList cpp_attributes = std::make_pair(
      kDummyCppAttributes, kNumDummyCppAttributes);
  CompilerInfoBuilder::FeatureList declspec_attributes = std::make_pair(
      kDummyDeclspecAttributes, kNumDummyDeclspecAttributes);
  CompilerInfoBuilder::FeatureList builtins = std::make_pair(
      kDummyBuiltins, kNumDummyBuiltins);

  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  EXPECT_TRUE(CompilerInfoBuilder::ParseFeatures(
                kClangOutput, object_macros, function_macros,
                features, extensions, attributes, cpp_attributes,
                declspec_attributes, builtins, cid.get()));
  CompilerInfo info(std::move(cid));

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
  EXPECT_EQ(201304, FindValue(info.has_cpp_attribute(),
                              "dummy_cpp_attribute1"));
  EXPECT_EQ(0U, info.has_cpp_attribute().count("dummy_cpp_attribute2"));
  EXPECT_EQ(201301, FindValue(info.has_cpp_attribute(),
                              "clang::dummy_cpp_attribute1"));
  EXPECT_EQ(0U, info.has_cpp_attribute().count("clang::dummy_cpp_attribute2"));

  EXPECT_EQ(1U, info.has_declspec_attribute().size());
  EXPECT_EQ(1, FindValue(info.has_declspec_attribute(),
                         "dummy_declspec_attributes1"));
  EXPECT_EQ(0U,
            info.has_declspec_attribute().count("dummy_declspec_attributes2"));

  EXPECT_EQ(1, FindValue(info.has_builtin(), "dummy_builtin1"));
  EXPECT_EQ(0U, info.has_builtin().count("dummy_builtin2"));
}


TEST_F(CompilerInfoTest, ParseRealClangPathForChromeOS) {
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

  const string path = CompilerInfoBuilder::ParseRealClangPath(
      kClangVoutput);
  EXPECT_EQ("/usr/local/google/home/test/usr/bin/clang-3.9",
            path);
}

TEST_F(CompilerInfoTest, ParseClangVersionTarget) {
  static const char kClangSharpOutput[] =
      "clang version 3.5 (trunk)\n"
      "Target: i686-pc-win32\n"
      "Thread model: posix\n";
  string version, target;
  EXPECT_TRUE(
      CompilerInfoBuilder::ParseClangVersionTarget(
          kClangSharpOutput,
          &version, &target));
  EXPECT_EQ("clang version 3.5 (trunk)", version);
  EXPECT_EQ("i686-pc-win32", target);
}

#ifdef _WIN32
TEST_F(CompilerInfoTest, SplitGccIncludeOutputForClang) {
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
  EXPECT_TRUE(CompilerInfoBuilder::SplitGccIncludeOutput(
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

TEST_F(CompilerInfoTest, SplitGccIncludeOutputForIQuote) {
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
  EXPECT_TRUE(CompilerInfoBuilder::SplitGccIncludeOutput(
      kGccVOutput, &qpaths, &paths, &framework_paths));

  const std::vector<string> expected_qpaths {
    "include",
  };
  EXPECT_EQ(expected_qpaths, qpaths);
  const std::vector<string> expected_paths {
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

::std::ostream& operator<<(
    ::std::ostream& os,
    const CompilerInfo::SubprogramInfo& info) {
  return os << info.DebugString();
}

TEST_F(CompilerInfoTest, GetExtraSubprogramsClangPlugin) {
  const string cwd("/");

  TmpdirUtil tmpdir("get_extra_subprograms_clang_plugin");
  tmpdir.SetCwd(cwd);
  tmpdir.CreateEmptyFile("libPlugin.so");

  std::vector<string> args, envs;
  args.push_back("/usr/bin/clang");
  args.push_back("-Xclang");
  args.push_back("-load");
  args.push_back("-Xclang");
  args.push_back(file::JoinPath(tmpdir.tmpdir(), "libPlugin.so"));
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, cwd);
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  CompilerInfoBuilder::ParseSubprogramFlags(
      "/usr/bin/clang", flags, &clang_plugins, &B_options, &no_integrated_as);
  std::vector<string> expected = {tmpdir.FullPath("libPlugin.so")};
  EXPECT_EQ(expected, clang_plugins);
  EXPECT_TRUE(B_options.empty());
  EXPECT_FALSE(no_integrated_as);
}

TEST_F(CompilerInfoTest, GetExtraSubprogramsClangPluginRelative) {
  const string cwd("/");

  TmpdirUtil tmpdir("get_extra_subprograms_clang_plugin");
  tmpdir.SetCwd(cwd);
  tmpdir.CreateEmptyFile("libPlugin.so");

  std::vector<string> args, envs;
  args.push_back("/usr/bin/clang");
  args.push_back("-Xclang");
  args.push_back("-load");
  args.push_back("-Xclang");
  args.push_back("libPlugin.so");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, cwd);
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  CompilerInfoBuilder::ParseSubprogramFlags(
      "/usr/bin/clang", flags, &clang_plugins, &B_options, &no_integrated_as);
  std::vector<string> expected = {"libPlugin.so"};
  EXPECT_EQ(expected, clang_plugins);
  EXPECT_TRUE(B_options.empty());
  EXPECT_FALSE(no_integrated_as);
}

TEST_F(CompilerInfoTest, GetExtraSubprogramsBOptions) {
  const string cwd("/");

  TmpdirUtil tmpdir("get_extra_subprograms_clang_plugin");
  tmpdir.SetCwd(cwd);
  tmpdir.CreateEmptyFile("libPlugin.so");

  std::vector<string> args, envs;
  args.push_back("/usr/bin/clang");
  args.push_back("-B");
  args.push_back("dummy");
  args.push_back("-c");
  args.push_back("hello.c");
  GCCFlags flags(args, cwd);
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  CompilerInfoBuilder::ParseSubprogramFlags(
      "/usr/bin/clang", flags, &clang_plugins, &B_options, &no_integrated_as);
  std::vector<string> expected = {"dummy"};
  EXPECT_TRUE(clang_plugins.empty());
  EXPECT_EQ(expected, B_options);
  EXPECT_FALSE(no_integrated_as);
}

TEST_F(CompilerInfoTest, ParseGetSubprogramsOutput) {
  const char kClangOutput[] =
      "clang version 3.5.0 (trunk 214024)\n"
      "Target: arm--linux\n"
      "Thread model: posix\n"
      " \"/usr/local/google/ssd/goma/chrome_src/src/third_party/"
      "llvm-build/Release+Asserts/bin/clang\" \"-cc1\" \"-triple\" \""
      "armv4t--linux\" \"-S\" \"-disable-free\" \"-main-file-name\" \""
      "null\" \"-mrelocation-model\" \"static\" \"-mdisable-fp-elim\" \""
      "-fmath-errno\" \"-masm-verbose\" \"-no-integrated-as\" \""
      "-mconstructor-aliases\" \"-target-cpu\" \"arm7tdmi\" \"-target-abi"
      "\" \"apcs-gnu\" \"-mfloat-abi\" \"hard\" \"-target-linker-version"
      "\" \"2.22\" \"-dwarf-column-info\" \"-coverage-file\" \"/tmp/null-"
      "6cb82c.s\" \"-resource-dir\" \"/usr/local/google/ssd/goma/"
      "chrome_src/src/third_party/llvm-build/Release+Asserts/bin/../lib/"
      "clang/3.5.0\" \"-internal-isystem\" \"/usr/lib/gcc/arm-linux-gnueabi/"
      "4.6/../../../../include/c++/4.6\" \"-internal-isystem\" \""
      "/usr/lib/gcc/arm-linux-gnueabi/4.6/../../../../include/c++/4.6/"
      "arm-linux-gnueabi\" \"-internal-isystem\" \"/usr/lib/gcc/arm-linux-"
      "gnueabi/4.6/../../../../include/c++/4.6/backward\" \""
      "-internal-isystem\" \"/usr/lib/gcc/arm-linux-gnueabi/4.6/../../../../"
      "include/arm-linux-gnueabi/c++/4.6\" \"-internal-isystem\" "
      "\"/usr/local/include\" \"-internal-isystem\" \"/usr/local/google/"
      "ssd/goma/chrome_src/src/third_party/llvm-build/Release+Asserts"
      "/bin/../lib/clang/3.5.0/include\" \"-internal-externc-isystem\" "
      "\"/include\" \"-internal-externc-isystem\" \"/usr/include\" "
      "\"-fdeprecated-macro\" \"-fno-dwarf-directory-asm\" "
      "\"-fdebug-compilation-dir\" \"/usr/local/google/home/goma/"
      ".ssd/chrome_src/src\" \"-ferror-limit\" \"19\" \"-fmessage-length\" "
      "\"0\" \"-mstackrealign\" \"-fno-signed-char\" \"-fobjc-runtime=gcc\" "
      "\"-fcxx-exceptions\" \"-fexceptions\" \"-fdiagnostics-show-option\" "
      "\"-o\" \"/tmp/null-6cb82c.s\" \"-x\" \"c++\" \"/dev/null\"\n"
      " \"/usr/lib/gcc/arm-linux-gnueabi/4.6/../../../../arm-linux-gnueabi"
      "/bin/as\" \"-mfloat-abi=hard\" \"-o\" \"/dev/null\" "
      "\"/tmp/null-6cb82c.s\"\n";

  std::vector<string> subprograms;
  std::vector<string> expected = {
    "/usr/lib/gcc/arm-linux-gnueabi/4.6/../../../../arm-linux-gnueabi/bin/as",
  };
  CompilerInfoBuilder::ParseGetSubprogramsOutput(
      kClangOutput, &subprograms);
  EXPECT_EQ(expected, subprograms);
}

TEST_F(CompilerInfoTest, ParseGetSubprogramsOutputWithAsSuffix) {
  const char kClangOutput[] =
    "clang version 3.5.0 (trunk 214024)\n"
    "Target: arm--linux-androideabi\n"
    "Thread model: posix\n"
    " \"/mnt/scratch0/b_used/build/slave/android_clang_dbg_recipe/build/src/"
    "third_party/llvm-build/Release+Asserts/bin/clang\" \"-cc1\" \"-triple"
    "\" \"armv6--linux-androideabi\" \"-S\" \"-disable-free\" \"-main-file-"
    "name\" \"null\" \"-mrelocation-model\" \"pic\" \"-pic-level\" \"2\" \""
    "-mdisable-fp-elim\" \"-relaxed-aliasing\" \"-fmath-errno\" \"-masm-"
    "verbose\" \"-no-integrated-as\" \"-mconstructor-aliases\" \"-munwind-"
    "tables\" \"-fuse-init-array\" \"-target-cpu\" \"cortex-a6\" \"-target-"
    "feature\" \"+soft-float-abi\" \"-target-feature\" \"+neon\" \"-target-"
    "abi\" \"aapcs-linux\" \"-mfloat-abi\" \"soft\" \"-target-linker-version"
    "\" \"1.22\" \"-dwarf-column-info\" \"-ffunction-sections\" \"-fdata"
    "-sections\" \"-coverage-file\" \"/tmp/null-c11ea4.s\" \"-resource-dir"
    "\" \"/mnt/scratch0/b_used/build/slave/android_clang_dbg_recipe/build"
    "/src/third_party/llvm-build/Release+Asserts/bin/../lib/clang/3.5.0\" "
    "\"-isystem\" \"/mnt/scratch0/b_used/build/slave/android_clang_dbg_"
    "recipe/build/src/third_party/android_tools/ndk//sources/cxx-stl/"
    "stlport/stlport\" \"-isysroot\" \"/mnt/scratch0/b_used/build/slave/"
    "android_clang_dbg_recipe/build/src/third_party/android_tools/ndk//"
    "platforms/android-14/arch-arm\" \"-internal-isystem\" \"/mnt/scratch0/"
    "b_used/build/slave/android_clang_dbg_recipe/build/src/third_party/"
    "android_tools/ndk//platforms/android-14/arch-arm/usr/local/include"
    "\" \"-internal-isystem\" \"/mnt/scratch0/b_used/build/slave/android_"
    "clang_dbg_recipe/build/src/third_party/llvm-build/Release+Asserts/bin/"
    "../lib/clang/3.5.0/include\" \"-internal-externc-isystem\" \"/mnt/"
    "scratch0/b_used/build/slave/android_clang_dbg_recipe/build/src/"
    "third_party/android_tools/ndk//platforms/android-14/arch-arm/include"
    "\" \"-internal-externc-isystem\" \"/mnt/scratch0/b_used/build/slave/"
    "android_clang_dbg_recipe/build/src/third_party/android_tools/ndk//"
    "platforms/android-14/arch-arm/usr/include\" \"-Os\" \"-std=gnu++11\" "
    "\"-fdeprecated-macro\" \"-fno-dwarf-directory-asm\" \"-fdebug-"
    "compilation-dir\" \"/mnt/scratch0/b_used/build/slave/android_clang_"
    "dbg_recipe/build/src/out/Debug\" \"-ferror-limit\" \"19\" \"-fmessage"
    "-length\" \"0\" \"-fvisibility\" \"hidden\" \"-fvisibility-inlines-"
    "hidden\" \"-fsanitize=address\" \"-stack-protector\" \"1\" \""
    "-mstackrealign\" \"-fno-rtti\" \"-fno-signed-char\" \"-fno-threadsafe"
    "-statics\" \"-fobjc-runtime=gcc\" \"-fdiagnostics-show-option\" \"-fcolor"
    "-diagnostics\" \"-vectorize-loops\" \"-vectorize-slp\" \"-load\" \"/mnt/"
    "scratch0/b_used/build/slave/android_clang_dbg_recipe/build/src/tools/"
    "clang/scripts/../../../third_party/llvm-build/Release+Asserts/lib/"
    "libFindBadConstructs.so\" \"-add-plugin\" \"find-bad-constructs\" \""
    "-mllvm\" \"-asan-globals=0\" \"-o\" \"/tmp/null-c11ea4.s\" \"-x\" \""
    "c++\" \"/dev/null\"\n"
    " \"/mnt/scratch0/b_used/build/slave/android_clang_dbg_recipe/build/src/"
    "third_party/android_tools/ndk//toolchains/arm-linux-androideabi-4.8/"
    "prebuilt/linux-x86_64/bin/arm-linux-androideabi-as\" \"-mfloat-abi="
    "softfp\" \"-march=armv7-a\" \"-mfpu=neon\" \"-o\" \"/dev/null\" \"/tmp/"
    "null-c11ea4.s\"\n";

  std::vector<string> subprograms;
  std::vector<string> expected = {
    "/mnt/scratch0/b_used/build/slave/android_clang_dbg_recipe/build/src/"
        "third_party/android_tools/ndk//toolchains/arm-linux-androideabi-4.8/"
        "prebuilt/linux-x86_64/bin/arm-linux-androideabi-as",
  };
  CompilerInfoBuilder::ParseGetSubprogramsOutput(
      kClangOutput, &subprograms);
  EXPECT_EQ(expected, subprograms);
}

TEST_F(CompilerInfoTest, ParseGetSubprogramsOutputShouldFailIfNoAs) {
  const char kClangOutput[] =
      "clang version 3.5.0 (trunk 214024)\n"
      "Target: arm--linux\n"
      "Thread model: posix\n"
      "clang: warning: unknown platform, assuming -mfloat-abi=soft\n"
      "clang: warning: unknown platform, assuming -mfloat-abi=soft\n"
      " \"/usr/local/google/ssd/goma/chrome_src/src/third_party/"
      "llvm-build/Release+Asserts/bin/clang\" \"-cc0\" \"-triple\" "
      "\"armv4t--linux\" \"-emit-obj\" \"-mrelax-all\" \"-disable-free\" "
      "\"-main-file-name\" \"null\" \"-mrelocation-model\" \"static\" "
      "\"-mdisable-fp-elim\" \"-fmath-errno\" \"-masm-verbose\" "
      "\"-mconstructor-aliases\" \"-target-cpu\" \"arm6tdmi\" "
      "\"-target-feature\" \"+soft-float\" \"-target-feature\" "
      "\"+soft-float-abi\" \"-target-feature\" \"-neon\" \"-target-feature\" "
      "\"-crypto\" \"-target-abi\" \"apcs-gnu\" \"-msoft-float\" "
      "\"-mfloat-abi\" \"soft\" \"-target-linker-version\" \"2.22\" "
      "\"-dwarf-column-info\" \"-coverage-file\" \"/dev/null\" "
      "\"-resource-dir\" \"/usr/local/google/ssd/goma/chrome_src/src/"
      "third_party/llvm-build/Release+Asserts/bin/../lib/clang/3.5.0\" "
      "\"-internal-isystem\" \"/usr/lib/gcc/arm-linux-gnueabi/4.6/../../"
      "../../include/c++/4.6\" \"-internal-isystem\" \"/usr/lib/gcc/"
      "arm-linux-gnueabi/4.6/../../../../include/c++/4.6/arm-linux-gnueabi\" "
      "\"-internal-isystem\" \"/usr/lib/gcc/arm-linux-gnueabi/4.6/../../"
      "../../include/c++/4.6/backward\" \"-internal-isystem\" \"/usr/lib/"
      "gcc/arm-linux-gnueabi/4.6/../../../../include/arm-linux-gnueabi/c++/"
      "4.6\" \"-internal-isystem\" \"/usr/local/include\" "
      "\"-internal-isystem\" \"/usr/local/google/ssd/goma/"
      "chrome_src/src/third_party/llvm-build/Release+Asserts/bin/../lib/"
      "clang/3.5.0/include\" \"-internal-externc-isystem\" \"/include\" "
      "\"-internal-externc-isystem\" \"/usr/include\" \"-fdeprecated-macro\" "
      "\"-fdebug-compilation-dir\" \"/usr/local/google/home/goma/"
      ".ssd/chrome_src/src\" \"-ferror-limit\" \"19\" \"-fmessage-length\" "
      "\"0\" \"-mstackrealign\" \"-fno-signed-char\" \"-fobjc-runtime=gcc\" "
      "\"-fcxx-exceptions\" \"-fexceptions\" \"-fdiagnostics-show-option\" "
      "\"-o\" \"/dev/null\" \"-x\" \"c++\" \"/dev/null\"\n";

  std::vector<string> subprograms;
  CompilerInfoBuilder::ParseGetSubprogramsOutput(
      kClangOutput, &subprograms);
  EXPECT_TRUE(subprograms.empty());
}

TEST_F(CompilerInfoTest, ParseGetSubprogramsOutputShouldGetSubprogWithPrefix) {
  const char kDummyClangOutput[] =
      " third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy "
      "--extract-dwo <file.o> <file.dwo>\n";
  std::vector<string> subprograms;
  CompilerInfoBuilder::ParseGetSubprogramsOutput(
      kDummyClangOutput, &subprograms);
  std::vector<string> expected = {
    "third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
        "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy"
  };
  EXPECT_EQ(expected, subprograms);
}

TEST_F(CompilerInfoTest, ParseGetSubprogramsOutputShouldDedupe) {
  const char kDummyClangOutput[] =
      " third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy "
      "--extract-dwo <file.o> <file.dwo>\n"
      " third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy "
      "/usr/bin/objcopy --strip-dwo <file.o>\n";
  std::vector<string> subprograms;
  CompilerInfoBuilder::ParseGetSubprogramsOutput(
      kDummyClangOutput, &subprograms);
  std::vector<string> expected = {
    "third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
        "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy"
  };
  EXPECT_EQ(expected, subprograms);
}

TEST_F(CompilerInfoTest, RewriteHashUnlockedEmptyRule) {
  std::map<std::string, std::string> rule;
  CompilerInfoData data;
  auto* sub = data.add_subprograms();
  sub->set_hash("dummy_hash");
  EXPECT_FALSE(CompilerInfoBuilder::RewriteHashUnlocked(rule, &data));
  EXPECT_EQ(1, data.subprograms_size());
  EXPECT_EQ("dummy_hash", data.subprograms(0).hash());
}

TEST_F(CompilerInfoTest, RewriteHashUnlockedNoMatchingRule) {
  std::map<std::string, std::string> rule;
  CHECK(rule.insert(std::make_pair("no_match", "no_match")).second);
  CompilerInfoData data;
  auto* sub = data.add_subprograms();
  sub->set_hash("dummy_hash");
  EXPECT_FALSE(CompilerInfoBuilder::RewriteHashUnlocked(rule, &data));
  EXPECT_EQ(1, data.subprograms_size());
  EXPECT_EQ("dummy_hash", data.subprograms(0).hash());
}

TEST_F(CompilerInfoTest, RewriteHashUnlockedMatchingRule) {
  std::map<std::string, std::string> rule;
  CHECK(rule.insert(std::make_pair("old_hash", "new_hash")).second);
  CompilerInfoData data;
  auto* sub = data.add_subprograms();
  sub->set_hash("old_hash");
  EXPECT_TRUE(CompilerInfoBuilder::RewriteHashUnlocked(rule, &data));
  EXPECT_EQ(1, data.subprograms_size());
  EXPECT_EQ("new_hash", data.subprograms(0).hash());
}

TEST_F(CompilerInfoTest, RewriteHashUnlockedBothMatchingAndNotMatching) {
  std::map<std::string, std::string> rule;
  CHECK(rule.insert(std::make_pair("old_hash", "new_hash")).second);
  CompilerInfoData data;
  auto* sub = data.add_subprograms();
  sub->set_hash("old_hash");
  auto* sub2 = data.add_subprograms();
  sub2->set_hash("yet_another_hash");
  EXPECT_TRUE(CompilerInfoBuilder::RewriteHashUnlocked(rule, &data));
  EXPECT_EQ(2, data.subprograms_size());
  EXPECT_EQ("new_hash", data.subprograms(0).hash());
  EXPECT_EQ("yet_another_hash", data.subprograms(1).hash());
}

TEST_F(CompilerInfoTest, GetCompilerNameUsualCases) {
  std::vector<std::pair<std::string, std::string>> test_cases = {
    {"clang", "clang"},
    {"clang++", "clang"},
    {"g++", "g++"},
    {"gcc", "gcc"},
  };

  for (const auto& tc : test_cases) {
    CompilerInfoData data;
    data.set_local_compiler_path(tc.first);
    data.set_real_compiler_path(tc.second);
    EXPECT_EQ(tc.first, CompilerInfoBuilder::GetCompilerName(data));
  }
}

TEST_F(CompilerInfoTest, GetCompilerNameCc) {
  std::vector<std::string> test_cases = {"clang", "gcc"};

  for (const auto& tc : test_cases) {
    CompilerInfoData data;
    data.set_local_compiler_path("cc");
    data.set_real_compiler_path(tc);
    EXPECT_EQ(tc, CompilerInfoBuilder::GetCompilerName(data));
  }
}

TEST_F(CompilerInfoTest, GetCompilerNameCxx) {
  CompilerInfoData data;
  data.set_local_compiler_path("c++");
  data.set_real_compiler_path("g++");
  EXPECT_EQ("g++", CompilerInfoBuilder::GetCompilerName(data));

  data.set_local_compiler_path("c++");
  data.set_real_compiler_path("clang");
  EXPECT_EQ("clang++", CompilerInfoBuilder::GetCompilerName(data));
}

TEST_F(CompilerInfoTest, GetCompilerNameUnsupportedCase) {
  CompilerInfoData data;
  data.set_local_compiler_path("c++");
  data.set_real_compiler_path("clang++");
  EXPECT_EQ("", CompilerInfoBuilder::GetCompilerName(data));
}

#ifdef __linux__
TEST_F(CompilerInfoTest, GetRealSubprogramPath) {
  TmpdirUtil tmpdir("get_real_subprogram_path");
  static const char kWrapperPath[] =
      "dummy/x86_64-cros-linux-gnu/binutils-bin/2.25.51-gold/objcopy";
  static const char kRealPath[] =
      "dummy/x86_64-cros-linux-gnu/binutils-bin/2.25.51/objcopy.elf";

  tmpdir.CreateEmptyFile(kWrapperPath);
  tmpdir.CreateEmptyFile(kRealPath);

  EXPECT_EQ(
      tmpdir.FullPath(kRealPath),
      CompilerInfoBuilder::GetRealSubprogramPath(
          tmpdir.FullPath(kWrapperPath)));
}
#endif

TEST_F(CompilerInfoTest, FillFromCompilerOutputsShouldUseProperPath) {
  std::vector<string> envs;
#ifdef _WIN32
  const string clang = file::JoinPath(TestDir(), "clang.bat");
  InstallReadCommandOutputFunc(ReadCommandOutputByRedirector);
  envs.emplace_back("PATHEXT=" + GetEnv("PATHEXT"));
#else
  const string clang = file::JoinPath(TestDir(), "clang");
  InstallReadCommandOutputFunc(ReadCommandOutputByPopen);
#endif
  std::vector<string> args = {
    clang,
  };
  envs.emplace_back("PATH=" + GetEnv("PATH"));
  std::unique_ptr<CompilerFlags> flags(CompilerFlags::MustNew(args, "."));
  CompilerInfoBuilder cib;
  std::unique_ptr<CompilerInfoData> data(
      cib.FillFromCompilerOutputs(*flags, clang, envs));
  EXPECT_TRUE(data.get());
  EXPECT_EQ(0, data->failed_at());
}

class ScopedCompilerInfoStateTest : public testing::Test {
 protected:
  void FillFromCompilerOutputs(ScopedCompilerInfoState* cis) {
    std::unique_ptr<CompilerInfoData> data(new CompilerInfoData);
    data->set_found(true);
    cis->reset(new CompilerInfoState(std::move(data)));
  }
};

TEST_F(ScopedCompilerInfoStateTest, reset) {
  ScopedCompilerInfoState cis;
  FillFromCompilerOutputs(&cis);
  EXPECT_TRUE(cis.get() != nullptr);
  EXPECT_EQ(1, cis.get()->refcnt());

  cis.reset(cis.get());
  EXPECT_TRUE(cis.get() != nullptr);
  EXPECT_EQ(1, cis.get()->refcnt());
}

}  // namespace devtools_goma
