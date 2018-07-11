// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cxx_compiler_info_builder.h"

#include "cxx_compiler_info.h"
#include "gtest/gtest.h"
#include "mypath.h"
#include "path.h"
#include "unittest_util.h"

namespace devtools_goma {

class CxxCompilerInfoBuilderTest : public testing::Test {
 protected:
  void SetUp() override { CheckTempDirectory(GetGomaTmpDir()); }

  void AppendPredefinedMacros(const string& macro, CompilerInfoData* cid) {
    cid->mutable_cxx()->set_predefined_macros(cid->cxx().predefined_macros() +
                                              macro);
  }

  string TestDir() {
    // This module is in out\Release.
    const std::string parent_dir = file::JoinPath(GetMyDirectory(), "..");
    const std::string top_dir = file::JoinPath(parent_dir, "..");
    return file::JoinPath(top_dir, "test");
  }
};

TEST_F(CxxCompilerInfoBuilderTest, IsCwdRelativeWithSubprogramInfo) {
  TmpdirUtil tmpdir("is_cwd_relative");
  tmpdir.CreateEmptyFile("as");

  CompilerInfoData::SubprogramInfo subprog_data;
  CxxCompilerInfoBuilder::SubprogramInfoFromPath(tmpdir.FullPath("as"),
                                                 &subprog_data);
  CompilerInfo::SubprogramInfo subprog;
  CompilerInfo::SubprogramInfo::FromData(subprog_data, &subprog);
  std::vector<CompilerInfo::SubprogramInfo> subprogs;
  subprogs.push_back(subprog);

  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->set_found(true);
  cid->add_subprograms()->CopyFrom(subprog_data);
  cid->mutable_cxx();

  CxxCompilerInfo info(std::move(cid));
  EXPECT_TRUE(info.IsCwdRelative(tmpdir.tmpdir()));
  EXPECT_FALSE(info.IsCwdRelative("/nonexistent"));
}

TEST_F(CxxCompilerInfoBuilderTest, ParseGetSubprogramsOutput) {
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
  CxxCompilerInfoBuilder::ParseGetSubprogramsOutput(kClangOutput, &subprograms);
  EXPECT_EQ(expected, subprograms);
}

TEST_F(CxxCompilerInfoBuilderTest, ParseGetSubprogramsOutputWithAsSuffix) {
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
      "-statics\" \"-fobjc-runtime=gcc\" \"-fdiagnostics-show-option\" "
      "\"-fcolor"
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
  CxxCompilerInfoBuilder::ParseGetSubprogramsOutput(kClangOutput, &subprograms);
  EXPECT_EQ(expected, subprograms);
}

TEST_F(CxxCompilerInfoBuilderTest, ParseGetSubprogramsOutputShouldFailIfNoAs) {
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
  CxxCompilerInfoBuilder::ParseGetSubprogramsOutput(kClangOutput, &subprograms);
  EXPECT_TRUE(subprograms.empty());
}

TEST_F(CxxCompilerInfoBuilderTest,
       ParseGetSubprogramsOutputShouldGetSubprogWithPrefix) {
  const char kDummyClangOutput[] =
      " third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy "
      "--extract-dwo <file.o> <file.dwo>\n";
  std::vector<string> subprograms;
  CxxCompilerInfoBuilder::ParseGetSubprogramsOutput(kDummyClangOutput,
                                                    &subprograms);
  std::vector<string> expected = {
      "third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy"};
  EXPECT_EQ(expected, subprograms);
}

TEST_F(CxxCompilerInfoBuilderTest, ParseGetSubprogramsOutputShouldDedupe) {
  const char kDummyClangOutput[] =
      " third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy "
      "--extract-dwo <file.o> <file.dwo>\n"
      " third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy "
      "/usr/bin/objcopy --strip-dwo <file.o>\n";
  std::vector<string> subprograms;
  CxxCompilerInfoBuilder::ParseGetSubprogramsOutput(kDummyClangOutput,
                                                    &subprograms);
  std::vector<string> expected = {
      "third_party/android_tools/ndk/toolchains/arm-linux-androideabi-4.9/"
      "prebuilt/linux-x86_64/bin/arm-linux-androideabi-objcopy"};
  EXPECT_EQ(expected, subprograms);
}

#ifdef __linux__
TEST_F(CxxCompilerInfoBuilderTest, GetRealSubprogramPath) {
  TmpdirUtil tmpdir("get_real_subprogram_path");
  static const char kWrapperPath[] =
      "dummy/x86_64-cros-linux-gnu/binutils-bin/2.25.51-gold/objcopy";
  static const char kRealPath[] =
      "dummy/x86_64-cros-linux-gnu/binutils-bin/2.25.51/objcopy.elf";

  tmpdir.CreateEmptyFile(kWrapperPath);
  tmpdir.CreateEmptyFile(kRealPath);

  EXPECT_EQ(tmpdir.FullPath(kRealPath),
            CxxCompilerInfoBuilder::GetRealSubprogramPath(
                tmpdir.FullPath(kWrapperPath)));
}
#endif

}  // namespace devtools_goma
