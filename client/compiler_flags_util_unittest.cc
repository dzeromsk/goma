// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_flags_util.h"

#include <memory>
#include <string>
#include <vector>

#include "compiler_info.h"
#include "gtest/gtest.h"

using std::string;

namespace devtools_goma {

class CompilerFlagsUtilTest : public testing::Test {
 protected:
  void SetSystemIncludePaths(
      const std::vector<string>& cxx_system_include_paths,
      const std::vector<string>& system_include_paths,
      const std::vector<string>& system_framework_paths,
      CompilerInfoData* compiler_info_data) {
    for (const auto& p : cxx_system_include_paths) {
      compiler_info_data->add_cxx_system_include_paths(p);
    }
    for (const auto& p : system_include_paths) {
      compiler_info_data->add_system_include_paths(p);
    }
    for (const auto& p : system_framework_paths) {
      compiler_info_data->add_system_framework_paths(p);
    }
  }
};

#ifndef _WIN32
TEST_F(CompilerFlagsUtilTest, MakeWeakRelativeMacWebKit) {
  const string cwd =
      "/Users/goma/src/chromium-webkit/src/third_party/WebKit/Source/WebKit";
  std::vector<string> args;
  args.push_back("/Developer/usr/bin/gcc-4.2");
  args.push_back("-x");
  args.push_back("objective-c");
  args.push_back("-arch");
  args.push_back("x86_64");
  args.push_back("-fmessage-length=0");
  args.push_back("-pipe");
  args.push_back("-std=gnu99");
  args.push_back("-Wno-trigraphs");
  args.push_back("-fpascal-strings");
  args.push_back("-O2");
  args.push_back("-Werror");
  args.push_back("-DNDEBUG");
  args.push_back("-fobjc-gc");
  args.push_back("-mmacosx-version-min=10.6");
  args.push_back("-gdwarf-2");
  args.push_back("-I/Users/goma/src/chromium-webkit/src/third_party/WebKit/"
                 "WebKitBuild/WebKit.build/Release/WebKit.build/WebKit.hmap");
  args.push_back("-Wall");
  args.push_back("-F/Users/goma/src/chromium-webkit/src/third_party/WebKit/"
                 "WebKitBuild/Release");
  args.push_back("-F/System/Library/Frameworks/WebKit.framework/Versions/A/"
                 "Frameworks");
  args.push_back("-I/Users/goma/src/chromium-webkit/src/third_party/WebKit/"
                 "WebKitBuild/Release/include");
  args.push_back("-include");
  args.push_back("/Users/goma/src/chromium-webkit/src/third_party/WebKit/"
                 "Source/WebKit/mac/WebKitPrefix.h");
  args.push_back("-imacros");
  args.push_back("/Users/goma/src/chromium-webkit/src/third_party/WebKit/"
                 "Source/WebKit/mac/WebKitPrefix2.h");
  args.push_back("-c");
  args.push_back("/Users/goma/src/chromium-webkit/src/third_party/WebKit/"
                 "Source/WebKit/mac/Misc/WebKitErrors.m");
  args.push_back("-o");
  args.push_back("/Users/goma/src/chromium-webkit/src/third_party/WebKit/"
                 "WebKitBuild/WebKit.build/Release/WebKit.build/"
                 "Objects-normal/x86_64/WebKitErrors.o");
  ASSERT_EQ(29U, args.size());

  std::unique_ptr<CompilerInfoData> compiler_info_data(new CompilerInfoData);
  {
    std::vector<string> cxx_system_include_paths;
    std::vector<string> system_include_paths;
    std::vector<string> system_framework_paths;

    cxx_system_include_paths.push_back("/usr/include/c++/4.2.1");
    system_include_paths.push_back(
        "/Developer/usr/bin/../lib/gcc/i686-apple-darwin10/4.2.1/include");
    system_include_paths.push_back(
        "/usr/lib/gcc/i686-apple-darwin10/4.2.1/include");
    system_include_paths.push_back("/usr/include");

    system_framework_paths.push_back("/System/Library/Frameworks");
    system_framework_paths.push_back("/Library/Frameworks");

    SetSystemIncludePaths(
        cxx_system_include_paths,
        system_include_paths,
        system_framework_paths,
        compiler_info_data.get());
  }

  CompilerInfo compiler_info(std::move(compiler_info_data));

  std::vector<string> parsed_args =
      CompilerFlagsUtil::MakeWeakRelative(
          args, cwd, compiler_info);
  ASSERT_EQ(args.size(), parsed_args.size());
  EXPECT_EQ("/Developer/usr/bin/gcc-4.2", parsed_args[0]);
  EXPECT_EQ("-x", parsed_args[1]);
  EXPECT_EQ("objective-c", parsed_args[2]);
  EXPECT_EQ("-arch", parsed_args[3]);
  EXPECT_EQ("x86_64", parsed_args[4]);
  EXPECT_EQ("-fmessage-length=0", parsed_args[5]);
  EXPECT_EQ("-pipe", parsed_args[6]);
  EXPECT_EQ("-std=gnu99", parsed_args[7]);
  EXPECT_EQ("-Wno-trigraphs", parsed_args[8]);
  EXPECT_EQ("-fpascal-strings", parsed_args[9]);
  EXPECT_EQ("-O2", parsed_args[10]);
  EXPECT_EQ("-Werror", parsed_args[11]);
  EXPECT_EQ("-DNDEBUG", parsed_args[12]);
  EXPECT_EQ("-fobjc-gc", parsed_args[13]);
  EXPECT_EQ("-mmacosx-version-min=10.6", parsed_args[14]);
  EXPECT_EQ("-gdwarf-2", parsed_args[15]);
  EXPECT_EQ("-I../../"
            "WebKitBuild/WebKit.build/Release/WebKit.build/WebKit.hmap",
            parsed_args[16]);
  EXPECT_EQ("-Wall", parsed_args[17]);
  EXPECT_EQ("-F../../WebKitBuild/Release", parsed_args[18]);
  EXPECT_EQ("-F/System/Library/Frameworks/WebKit.framework/Versions/A/"
            "Frameworks", parsed_args[19]);
  EXPECT_EQ("-I../../WebKitBuild/Release/include", parsed_args[20]);
  EXPECT_EQ("-include", parsed_args[21]);
  EXPECT_EQ("mac/WebKitPrefix.h", parsed_args[22]);
  EXPECT_EQ("-imacros", parsed_args[23]);
  EXPECT_EQ("mac/WebKitPrefix2.h", parsed_args[24]);
  EXPECT_EQ("-c", parsed_args[25]);
  EXPECT_EQ("mac/Misc/WebKitErrors.m", parsed_args[26]);
  EXPECT_EQ("-o", parsed_args[27]);
  EXPECT_EQ("../../WebKitBuild/WebKit.build/Release/WebKit.build/"
            "Objects-normal/x86_64/WebKitErrors.o", parsed_args[28]);
}

TEST_F(CompilerFlagsUtilTest, MakeWeakRelativeChromiumClang) {
  const string cwd = "/home/goma/src/chromium1/src";
  std::vector<string> args;
  args.push_back("clang++");
  args.push_back("-DNO_HEAPCHECKER");
  args.push_back("-DENABLE_REMOTING=1");
  args.push_back("-DGR_GL_CUSTOM_SETUP_HEADER=\"GrGLConfig_chrome.h\"");
  args.push_back("-Ithird_party/icu/public/common");
  args.push_back("-I/usr/include/gtk-2.0");
  args.push_back("-Wno-unnamed-type-template-args");
  args.push_back("-O2");
  args.push_back("-Xclang");
  args.push_back("-load");
  args.push_back("-Xclang");
  args.push_back("/home/goma/src/chromium1/src/"
                 "tools/clang/scripts/../../../"
                 "third_party/llvm-build/Release+Asserts/lib/"
                 "libFindBadConstructs.so");
  args.push_back("-Xclang");
  args.push_back("-add-plugin");
  args.push_back("-Xclang");
  args.push_back("find-bad-constructs");
  args.push_back("-fdata-sections");
  args.push_back("-ffunction-sections");
  args.push_back("-MMD");
  args.push_back("-MF");
  args.push_back("llvm/Release/.deps/llvm/Release/obj.target/"
                 "common/chrome/common/about_handler.o.d.raw");
  args.push_back("-c");
  args.push_back("-o");
  args.push_back("llvm/Release/obj.target"
                 "/common/chrome/common/about_handler.o");
  args.push_back("chrome/common/about_handler.cc");
  ASSERT_EQ(25U, args.size());

  std::unique_ptr<CompilerInfoData> compiler_info_data(new CompilerInfoData);
  {
    std::vector<string> cxx_system_include_paths;
    cxx_system_include_paths.push_back("/usr/include/c++/4.4.3");
    std::vector<string> system_include_paths;
    system_include_paths.push_back("/usr/include");
    std::vector<string> system_framework_paths;
    SetSystemIncludePaths(
        cxx_system_include_paths,
        system_include_paths,
        system_framework_paths,
        compiler_info_data.get());
  }

  CompilerInfo compiler_info(std::move(compiler_info_data));

  std::vector<string> parsed_args =
      CompilerFlagsUtil::MakeWeakRelative(
          args, cwd, compiler_info);
  ASSERT_EQ(25U, parsed_args.size());
  EXPECT_EQ("clang++", parsed_args[0]);
  EXPECT_EQ("-DNO_HEAPCHECKER", parsed_args[1]);
  EXPECT_EQ("-DENABLE_REMOTING=1", parsed_args[2]);
  EXPECT_EQ("-DGR_GL_CUSTOM_SETUP_HEADER=\"GrGLConfig_chrome.h\"",
            parsed_args[3]);
  EXPECT_EQ("-Ithird_party/icu/public/common", parsed_args[4]);
  EXPECT_EQ("-I/usr/include/gtk-2.0", parsed_args[5]);
  EXPECT_EQ("-Wno-unnamed-type-template-args", parsed_args[6]);
  EXPECT_EQ("-O2", parsed_args[7]);
  EXPECT_EQ("-Xclang", parsed_args[8]);
  EXPECT_EQ("-load", parsed_args[9]);
  EXPECT_EQ("-Xclang", parsed_args[10]);
  EXPECT_EQ("tools/clang/scripts/../../../"
            "third_party/llvm-build/Release+Asserts/lib/"
            "libFindBadConstructs.so", parsed_args[11]);
  EXPECT_EQ("-Xclang", parsed_args[12]);
  EXPECT_EQ("-add-plugin", parsed_args[13]);
  EXPECT_EQ("-Xclang", parsed_args[14]);
  EXPECT_EQ("find-bad-constructs", parsed_args[15]);
  EXPECT_EQ("-fdata-sections", parsed_args[16]);
  EXPECT_EQ("-ffunction-sections", parsed_args[17]);
  EXPECT_EQ("-MMD", parsed_args[18]);
  EXPECT_EQ("-MF", parsed_args[19]);
  EXPECT_EQ("llvm/Release/.deps/llvm/Release/obj.target/"
            "common/chrome/common/about_handler.o.d.raw", parsed_args[20]);
  EXPECT_EQ("-c", parsed_args[21]);
  EXPECT_EQ("-o", parsed_args[22]);
  EXPECT_EQ("llvm/Release/obj.target/common/chrome/common/about_handler.o",
            parsed_args[23]);
  EXPECT_EQ("chrome/common/about_handler.cc", parsed_args[24]);
}
#endif

}  // namespace devtools_goma
