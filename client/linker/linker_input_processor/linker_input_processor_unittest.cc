// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifdef _WIN32
# include "config_win.h"
# include <shlobj.h>
#endif

#include <limits.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "compiler_info.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "ioutil.h"
#include "library_path_resolver.h"
#include "linker_input_processor.h"
#include "path.h"
#include "path_util.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

static const char *kElfBinary = "\177ELF\002\001\001\001blahblahblah";
static const char *kArFile = "!<arch>\n/        ";
static const char *kThinArFile = "!<thin>\n/        ";
#ifdef __MACH__
static const char *kMachOFatFile = "\xca\xfe\xba\xbe blahblahblah";
static const char *kMachMagic = "\xfe\xed\xfa\xce blahblahblah";
static const char *kMachCigam = "\xce\xfa\xed\xfe blahblahblah";
static const char *kMachMagic64 = "\xfe\xed\xfa\xcf blahblahblah";
static const char *kMachCigam64 = "\xcf\xfa\xed\xfe blahblahblah";
#endif

class LinkerInputProcessorTest : public testing::Test {
 public:
  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("linker_input_processor_test");

    // To be used by LibraryPathResolver::fakeroot_.
    tmpdir_ = tmpdir_util_->tmpdir();
    LibraryPathResolver::fakeroot_ = tmpdir_.c_str();
  }

  void TearDown() override {
    LibraryPathResolver::fakeroot_ = "";
    tmpdir_util_.reset();
  }

  bool ParseDumpOutput(const string& dump_output,
                       std::vector<string>* driver_args,
                       std::vector<string>* driver_envs) {
    return LinkerInputProcessor::ParseDumpOutput(dump_output, driver_args,
                                                 driver_envs);
  }

  void ParseDriverCommandLine(
      const std::vector<string>& driver_args,
      const string& cwd,
      string* sysroot,
      string* arch,
      std::vector<string>* searchdirs,
      std::vector<string>* input_paths) {
    LinkerInputProcessor linker_input_processor(cwd);
    linker_input_processor.ParseDriverCommandLine(driver_args, input_paths);

    *sysroot = linker_input_processor.library_path_resolver_->sysroot();
    *arch = linker_input_processor.arch_;
    const std::vector<string>& parsed_searchdirs =
      linker_input_processor.library_path_resolver_->searchdirs();
    copy(parsed_searchdirs.begin(), parsed_searchdirs.end(),
         back_inserter(*searchdirs));
  }

  LinkerInputProcessor::FileType CheckFileType(const string& path) {
    return LinkerInputProcessor::CheckFileType(
        tmpdir_util_->FullPath(path));
  }

  void GetLibraryPath(
      const std::vector<string>& envs,
      const string& cwd,
      const std::vector<string>& searchdirs,
      std::vector<string>* library_paths) {
    LinkerInputProcessor linker_input_processor(cwd);
    linker_input_processor.library_path_resolver_->AppendSearchdirs(
        searchdirs);
    linker_input_processor.GetLibraryPath(envs, library_paths);
  }

  void ParseThinArchive(const string& filename, std::set<string>* input_files) {
    std::set<string> raw_input_files;
    LinkerInputProcessor::ParseThinArchive(tmpdir_ + filename,
                                           &raw_input_files);
    for (const auto& iter : raw_input_files) {
      VLOG(1) << "input_files:" << iter;
      EXPECT_TRUE(HasPrefixDir(iter, tmpdir_));
      input_files->insert(iter.substr(tmpdir_.size()));
    }
  }

#ifndef _WIN32
  void Archive(const string& cwd, const string& op, const string& archive,
               const std::vector<string>& files) {
    tmpdir_util_->MkdirForPath(cwd, true);
    std::stringstream ss;
    ss << "cd " << tmpdir_ << cwd << " && ar " << op << " " << archive;
    for (const auto& file : files) {
      ss << " " << file;
    }
    PCHECK(system(ss.str().c_str()) == 0) << ss.str();
  }
#endif

 protected:
  std::unique_ptr<TmpdirUtil> tmpdir_util_;

  std::string tmpdir_;
};

TEST_F(LinkerInputProcessorTest, ParseGccDumpOutput) {
  std::vector<string> driver_args;
  std::vector<string> driver_envs;
  EXPECT_TRUE(ParseDumpOutput(
      "Using built-in specs.\n"
      "Target: x86_64-linux-gnu\n"
      "Configured with: ../src/configure -v "
      "--with-pkgversion='Ubuntu 4.4.3-4ubuntu5' "
      "--with-bugurl=file:///usr/share/doc/gcc-4.4/README.Bugs "
      "--enable-languages=c,c++,fortran,objc,obj-c++ --prefix=/usr\n"
      "Thread model: posix\n"
      "gcc version 4.4.3 (Ubuntu 4.4.3-4ubuntu5) \n"
      "COMPILER_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu\n"
      "LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/\n"
      "COLLECT_GCC_OPTIONS='-pthread' '-Lout/Release' '-L/lib' '-o' "
      "'out/Release/chrome' '-shared-libgcc' '-mtune=generic'\n"
      " \"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/collect2\" \"--build-id\" "
      "\"--eh-frame-hdr\" \"-m\" \"elf_x86_64\" \"--hash-style=both\" "
      "\"-dynamic-linker\" \"/lib64/ld-linux-x86-64.so.2\" "
      "\"-o\" \"out/Release/chrome\" \"-z\" \"relro\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbegin.o\" "
      "\"-Lout/Release\" \"-L/lib\" \"-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3\" "
      "\"-O1\" \"--as-needed\" \"--gc-sections\" \"--icf=safe\" "
      "\"--start-group\" "
      "\"out/Release/obj.target/chrome/chrome/app/chrome_main.o\" "
      "\"out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a\" "
      "\"--end-group\" \"-lX11\" \"-ldl\" \"-lXrender\" \"-lXss\" "
      "\"-lstdc++\" \"-lm\" \"-lgcc_s\" \"-lgcc\" \"-lpthread\" \"-lc\" "
      "\"-lgcc_s\" \"-lgcc\" \"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o\"\n",
      &driver_args, &driver_envs));

  std::vector<string> expected_args;
  expected_args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/collect2");
  expected_args.push_back("--build-id");
  expected_args.push_back("--eh-frame-hdr");
  expected_args.push_back("-m");
  expected_args.push_back("elf_x86_64");
  expected_args.push_back("--hash-style=both");
  expected_args.push_back("-dynamic-linker");
  expected_args.push_back("/lib64/ld-linux-x86-64.so.2");
  expected_args.push_back("-o");
  expected_args.push_back("out/Release/chrome");
  expected_args.push_back("-z");
  expected_args.push_back("relro");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbegin.o");
  expected_args.push_back("-Lout/Release");
  expected_args.push_back("-L/lib");
  expected_args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  expected_args.push_back("-O1");
  expected_args.push_back("--as-needed");
  expected_args.push_back("--gc-sections");
  expected_args.push_back("--icf=safe");
  expected_args.push_back("--start-group");
  expected_args.push_back(
      "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  expected_args.push_back(
      "out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a");
  expected_args.push_back("--end-group");
  expected_args.push_back("-lX11");
  expected_args.push_back("-ldl");
  expected_args.push_back("-lXrender");
  expected_args.push_back("-lXss");
  expected_args.push_back("-lstdc++");
  expected_args.push_back("-lm");
  expected_args.push_back("-lgcc_s");
  expected_args.push_back("-lgcc");
  expected_args.push_back("-lpthread");
  expected_args.push_back("-lc");
  expected_args.push_back("-lgcc_s");
  expected_args.push_back("-lgcc");
  expected_args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o");
  EXPECT_EQ(expected_args, driver_args);

  std::vector<string> expected_envs;
  expected_envs.push_back(
      "COMPILER_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:/usr/lib/gcc/x86_64-linux-gnu");
  expected_envs.push_back(
      "LIBRARY_PATH=/usr/lib/gcc/x86_64-linux-gnu/4.4.3/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/");
  EXPECT_EQ(expected_envs, driver_envs);
}

TEST_F(LinkerInputProcessorTest, ParseGcc46DumpOutput) {
  std::vector<string> driver_args;
  std::vector<string> driver_envs;
  EXPECT_TRUE(ParseDumpOutput(
    "Using built-in specs.\n"
    "COLLECT_GCC=/usr/bin/g++\n"
    "COLLECT_LTO_WRAPPER=/usr/lib/gcc/x86_64-linux-gnu/4.6/lto-wrapper\n"
    "Target: x86_64-linux-gnu\n"
    "Configured with: ../src/configure -v --with-pkgversion='Ubuntu/Linaro"
    " 4.6.3-1ubuntu5' "
    "--with-bugurl=file:///usr/share/doc/gcc-4.6/README.Bugs "
    "--enable-languages=c,c++,fortran,objc,obj-c++ --prefix=/usr "
    "--program-suffix=-4.6 --enable-shared --enable-linker-build-id "
    "--with-system-zlib --libexecdir=/usr/lib --without-included-gettext "
    "--enable-threads=posix --with-gxx-include-dir=/usr/include/c++/4.6 "
    "--libdir=/usr/lib --enable-nls --with-sysroot=/ --enable-clocale=gnu "
    "--enable-libstdcxx-debug --enable-libstdcxx-time=yes "
    "--enable-gnu-unique-object --enable-plugin --enable-objc-gc "
    "--disable-werror --with-arch-32=i686 --with-tune=generic "
    "--enable-checking=release --build=x86_64-linux-gnu "
    "--host=x86_64-linux-gnu --target=x86_64-linux-gnu\n"
    "Thread model: posix\n"
    "gcc version 4.6.3 (Ubuntu/Linaro 4.6.3-1ubuntu5) \n"
    "COMPILER_PATH=../../third_party/gold/:"
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
    "/usr/lib/gcc/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
    "/usr/lib/gcc/x86_64-linux-gnu/\n"
    "LIBRARY_PATH=../../third_party/gold/:"
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/:"
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../lib/:"
    "/lib/x86_64-linux-gnu/:/lib/../lib/:/usr/lib/x86_64-linux-gnu/:"
    "/usr/lib/../lib/:/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../:/lib/:"
    "/usr/lib/\n"
    "COLLECT_GCC_OPTIONS='-pthread' '-fPIC' '-B' '../../third_party/gold' "
    "'-o' 'codesighs' '-shared-libgcc' '-mtune=generic' '-march=x86-64'\n"
    " /usr/lib/gcc/x86_64-linux-gnu/4.6/collect2 \"--sysroot=/\" "
    "--build-id --no-add-needed --as-needed --eh-frame-hdr -m elf_x86_64 "
    "\"--hash-style=gnu\" -dynamic-linker /lib64/ld-linux-x86-64.so.2 "
    "-z relro -o codesighs "
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/crt1.o "
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/crti.o "
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/crtbegin.o -L../../third_party/gold "
    "-L/usr/lib/gcc/x86_64-linux-gnu/4.6 "
    "-L/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu "
    "-L/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../lib "
    "-L/lib/x86_64-linux-gnu -L/lib/../lib -L/usr/lib/x86_64-linux-gnu "
    "-L/usr/lib/../lib -L/usr/lib/gcc/x86_64-linux-gnu/4.6/../../.. "
    "-z noexecstack --threads \"--thread-count=4\" \"--icf=none\" "
    "\"-rpath=$ORIGIN/lib\" "
    "--start-group obj/third_party/codesighs/codesighs.codesighs.o "
    "--end-group \"-lstdc++\" -lm -lgcc_s -lgcc -lpthread -lc -lgcc_s "
    "-lgcc /usr/lib/gcc/x86_64-linux-gnu/4.6/crtend.o "
    "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/crtn.o\n",
      &driver_args, &driver_envs));

  std::vector<string> expected_args;
  expected_args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6/collect2");
  expected_args.push_back("--sysroot=/");
  expected_args.push_back("--build-id");
  expected_args.push_back("--no-add-needed");
  expected_args.push_back("--as-needed");
  expected_args.push_back("--eh-frame-hdr");
  expected_args.push_back("-m");
  expected_args.push_back("elf_x86_64");
  expected_args.push_back("--hash-style=gnu");
  expected_args.push_back("-dynamic-linker");
  expected_args.push_back("/lib64/ld-linux-x86-64.so.2");
  expected_args.push_back("-z");
  expected_args.push_back("relro");
  expected_args.push_back("-o");
  expected_args.push_back("codesighs");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/crt1.o");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/crti.o");
  expected_args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6/crtbegin.o");
  expected_args.push_back("-L../../third_party/gold");
  expected_args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.6");
  expected_args.push_back(
      "-L/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu");
  expected_args.push_back(
      "-L/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../lib");
  expected_args.push_back("-L/lib/x86_64-linux-gnu");
  expected_args.push_back("-L/lib/../lib");
  expected_args.push_back("-L/usr/lib/x86_64-linux-gnu");
  expected_args.push_back("-L/usr/lib/../lib");
  expected_args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.6/../../..");
  expected_args.push_back("-z");
  expected_args.push_back("noexecstack");
  expected_args.push_back("--threads");
  expected_args.push_back("--thread-count=4");
  expected_args.push_back("--icf=none");
  expected_args.push_back("-rpath=$ORIGIN/lib");
  expected_args.push_back("--start-group");
  expected_args.push_back("obj/third_party/codesighs/codesighs.codesighs.o");
  expected_args.push_back("--end-group");
  expected_args.push_back("-lstdc++");
  expected_args.push_back("-lm");
  expected_args.push_back("-lgcc_s");
  expected_args.push_back("-lgcc");
  expected_args.push_back("-lpthread");
  expected_args.push_back("-lc");
  expected_args.push_back("-lgcc_s");
  expected_args.push_back("-lgcc");
  expected_args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6/crtend.o");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/crtn.o");
  EXPECT_EQ(expected_args, driver_args);

  std::vector<string> expected_envs;
  expected_envs.push_back(
      "COMPILER_PATH=../../third_party/gold/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/");
  expected_envs.push_back(
      "LIBRARY_PATH=../../third_party/gold/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../lib/:"
      "/lib/x86_64-linux-gnu/:/lib/../lib/:/usr/lib/x86_64-linux-gnu/:"
      "/usr/lib/../lib/:/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../:/lib/:"
      "/usr/lib/");
  EXPECT_EQ(expected_envs, driver_envs);
}

TEST_F(LinkerInputProcessorTest, ParseGccErrorDumpOutput) {
  std::vector<string> driver_args;
  std::vector<string> driver_envs;
  EXPECT_FALSE(ParseDumpOutput(
      "g++: out/Release/obj.target/memory_test/"
      "chrome/test/memory_test/memory_test.o: No such file or directory\n"
      "g++: out/Release/obj.target/chrome/libtest_support_common.a: "
      "No such file or directory\n"
      "\n"
      "Using built-in specs.\n"
      "Target: x86_64-linux-gnu\n"
      "Configured with: ../src/configure -v "
      "--with-pkgversion='Ubuntu 4.4.3-4ubuntu5' "
      "--with-bugurl=file:///usr/share/doc/gcc-4.4/README.Bugs "
      "--enable-languages=c,c++,fortran,objc,obj-c++ --prefix=/usr\n"
      "Thread model: posix\n"
      "gcc version 4.4.3 (Ubuntu 4.4.3-4ubuntu5)\n",
      &driver_args, &driver_envs));
}

TEST_F(LinkerInputProcessorTest, ParseClangDumpOutput) {
  std::vector<string> driver_args;
  std::vector<string> driver_envs;
  EXPECT_TRUE(ParseDumpOutput(
      "clang version 3.0 (trunk 131935)\n"
      "Target: x86_64-unknown-linux-gnu\n"
      "Thread model: posix\n"
      " \"/usr/bin/ld\" \"-z\" \"relro\" \"--hash-style=both\" "
      "\"--build-id\" \"--eh-frame-hdr\" \"-m\" \"elf_x86_64\" "
      "\"-dynamic-linker\" \"/lib64/ld-linux-x86-64.so.2\" "
      "\"-o\" \"out/Release/chrome\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64/crt1.o\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64/crti.o\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbegin.o\" "
      "\"-Lout/Release\" \"-L/lib\" \"-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3\" "
      "\"-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64\" "
      "\"-L/lib/../lib64\" \"-L/usr/lib/../lib64\" "
      "\"-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../..\" "
      "\"-L/usr/lib/x86_64-linux-gnu\" \"-z\" \"noexecstack\" "
      "\"-O1\" \"--as-needed\" \"--gc-sections\" \"--icf=safe\" "
      "\"--start-group\" "
      "\"out/Release/obj.target/chrome/chrome/app/chrome_main.o\" "
      "\"out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a\" "
      "\"--end-group\" \"-lX11\" \"-ldl\" \"-lXrender\" \"-lXss\" "
      "\"-lstdc++\" \"-lm\" \"-lgcc_s\" \"-lgcc\" \"-lpthread\" "
      "\"-lc\" \"-lgcc_s\" \"-lgcc\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o\" "
      "\"/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64/crtn.o\"\n",
      &driver_args, &driver_envs));
  std::vector<string> expected_args;
  expected_args.push_back("/usr/bin/ld");
  expected_args.push_back("-z");
  expected_args.push_back("relro");
  expected_args.push_back("--hash-style=both");
  expected_args.push_back("--build-id");
  expected_args.push_back("--eh-frame-hdr");
  expected_args.push_back("-m");
  expected_args.push_back("elf_x86_64");
  expected_args.push_back("-dynamic-linker");
  expected_args.push_back("/lib64/ld-linux-x86-64.so.2");
  expected_args.push_back("-o");
  expected_args.push_back("out/Release/chrome");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64/crt1.o");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64/crti.o");
  expected_args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbegin.o");
  expected_args.push_back("-Lout/Release");
  expected_args.push_back("-L/lib");
  expected_args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  expected_args.push_back(
      "-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64");
  expected_args.push_back("-L/lib/../lib64");
  expected_args.push_back("-L/usr/lib/../lib64");
  expected_args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../..");
  expected_args.push_back("-L/usr/lib/x86_64-linux-gnu");
  expected_args.push_back("-z");
  expected_args.push_back("noexecstack");
  expected_args.push_back("-O1");
  expected_args.push_back("--as-needed");
  expected_args.push_back("--gc-sections");
  expected_args.push_back("--icf=safe");
  expected_args.push_back("--start-group");
  expected_args.push_back(
      "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  expected_args.push_back(
      "out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a");
  expected_args.push_back("--end-group");
  expected_args.push_back("-lX11");
  expected_args.push_back("-ldl");
  expected_args.push_back("-lXrender");
  expected_args.push_back("-lXss");
  expected_args.push_back("-lstdc++");
  expected_args.push_back("-lm");
  expected_args.push_back("-lgcc_s");
  expected_args.push_back("-lgcc");
  expected_args.push_back("-lpthread");
  expected_args.push_back("-lc");
  expected_args.push_back("-lgcc_s");
  expected_args.push_back("-lgcc");
  expected_args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o");
  expected_args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib64/crtn.o");
  EXPECT_EQ(expected_args, driver_args);

  std::vector<string> expected_envs;
  EXPECT_EQ(expected_envs, driver_envs);
}

#ifdef __linux__
TEST_F(LinkerInputProcessorTest, ParseGccDriverCommandLine) {
  string cwd = "/src";
  tmpdir_util_->SetCwd(cwd);
  tmpdir_util_->CreateTmpFile("/lib64/ld-linux-x86-64.so.2", kElfBinary);
  tmpdir_util_->CreateTmpFile(file::JoinPath(cwd, "out/Release/chrome"), "");
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o",
      kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o",
      kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbegin.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(file::JoinPath(cwd, "out/Release/.tmp"), "");
  tmpdir_util_->CreateTmpFile(
      file::JoinPath(cwd,
                     "out/Release/obj.target/chrome/chrome/app/chrome_main.o"),
      kElfBinary);
  tmpdir_util_->CreateTmpFile(
      file::JoinPath(
          cwd, "out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a"),
      kThinArFile);
  tmpdir_util_->CreateTmpFile("/usr/lib/libX11.so", kElfBinary);
  tmpdir_util_->CreateTmpFile("/usr/lib/libdl.so", kElfBinary);
  tmpdir_util_->CreateTmpFile("/usr/lib/libXrender.so", kElfBinary);
  tmpdir_util_->CreateTmpFile("/usr/lib/libXss.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libstdc++.so", kElfBinary);
  tmpdir_util_->CreateTmpFile("/usr/lib/libm.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc_s.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc.a", kArFile);
  tmpdir_util_->CreateTmpFile("/usr/lib/libpthread.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/libc.so", "OUTPUT_FORMAT(elf64-x86-64)");
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o",
      kElfBinary);

  std::vector<string> args;
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/collect2");
  args.push_back("--build-id");
  args.push_back("--eh-frame-hdr");
  args.push_back("-m");
  args.push_back("elf_x86_64");
  args.push_back("--hash-style=both");
  args.push_back("-dynamic-linker");
  args.push_back("/lib64/ld-linux-x86-64.so.2");
  args.push_back("-o");
  args.push_back("out/Release/chrome");
  args.push_back("-z");
  args.push_back("relro");
  args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o");
  args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o");
  args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbegin.o");
  args.push_back("-Lout/Release");
  args.push_back("-L/lib");
  args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  args.push_back("-L/lib/../lib");
  args.push_back("-L/usr/lib/../lib");
  args.push_back("-O1");
  args.push_back("--as-needed");
  args.push_back("--gc-sections");
  args.push_back("--icf=safe");
  args.push_back("--start-group");
  args.push_back(
      "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  args.push_back(
      "out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a");
  args.push_back("--end-group");
  args.push_back("-lX11");
  args.push_back("-ldl");
  args.push_back("-lXrender");
  args.push_back("-lXss");
  args.push_back("-lstdc++");
  args.push_back("-lm");
  args.push_back("-lgcc_s");
  args.push_back("-lgcc");
  args.push_back("-lpthread");
  args.push_back("-lc");
  args.push_back("-lgcc_s");
  args.push_back("-lgcc");
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o");
  args.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o");

  std::vector<string> input_paths;
  std::vector<string> searchdirs;
  string sysroot;
  string arch;
  ParseDriverCommandLine(
      args, cwd, &sysroot, &arch, &searchdirs, &input_paths);
  std::vector<string> expected_paths;
  expected_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o");
  expected_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbegin.o");
  expected_paths.push_back(
      "out/Release/obj.target/chrome/chrome/app/chrome_main.o");
  expected_paths.push_back(
      "out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o");
  expected_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o");
  expected_paths.push_back("/lib64/ld-linux-x86-64.so.2");
  expected_paths.push_back("/usr/lib/../lib/libX11.so");
  expected_paths.push_back("/usr/lib/../lib/libdl.so");
  expected_paths.push_back("/usr/lib/../lib/libXrender.so");
  expected_paths.push_back("/usr/lib/../lib/libXss.so");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libstdc++.so");
  expected_paths.push_back("/usr/lib/../lib/libm.so");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc_s.so");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc.a");
  expected_paths.push_back("/usr/lib/../lib/libpthread.so");
  expected_paths.push_back("/usr/lib/../lib/libc.so");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc_s.so");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc.a");

  EXPECT_EQ(expected_paths, input_paths);

  std::vector<string> expected_searchdirs;
  expected_searchdirs.push_back("out/Release");
  expected_searchdirs.push_back("/lib");
  expected_searchdirs.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  expected_searchdirs.push_back("/lib/../lib");
  expected_searchdirs.push_back("/usr/lib/../lib");
  EXPECT_EQ(expected_searchdirs, searchdirs);

  EXPECT_EQ("", sysroot);
  EXPECT_EQ("", arch);
}

TEST_F(LinkerInputProcessorTest, ParseGccDriverCommandLineStaticLink) {
  string cwd = "/src";
  tmpdir_util_->SetCwd(cwd);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbeginT.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(file::JoinPath(cwd, "hello.o"), kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc.a", kArFile);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc_eh.a", kArFile);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/libc.so", "OUTPUT_FORMAT(elf64-x86-64)");
  tmpdir_util_->CreateTmpFile("/usr/lib/libc.a", kArFile);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o", kElfBinary);

  std::vector<string> args;
  // gcc -### -static -o hello hello.o
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/collect2");
  args.push_back("--build-id");
  args.push_back("-m");
  args.push_back("elf_x86_64");
  args.push_back("--hash-style=both");
  args.push_back("-static");
  args.push_back("-o");
  args.push_back("hello");
  args.push_back("-z");
  args.push_back("relro");
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o");
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o");
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbeginT.o");
  args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib");
  args.push_back("-L/lib/../lib");
  args.push_back("-L/usr/lib/../lib");
  args.push_back("-L/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../..");
  args.push_back("-L/usr/lib/x86_64-linux-gnu");
  args.push_back("hello.o");
  args.push_back("--start-group");
  args.push_back("-lgcc");
  args.push_back("-lgcc_eh");
  args.push_back("-lc");
  args.push_back("--end-group");
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o");
  args.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o");

  std::vector<string> input_paths;
  std::vector<string> searchdirs;
  string sysroot;
  string arch;
  ParseDriverCommandLine(
      args, cwd, &sysroot, &arch, &searchdirs, &input_paths);
  std::vector<string> expected_paths;
  expected_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crt1.o");
  expected_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crti.o");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtbeginT.o");
  expected_paths.push_back("hello.o");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/crtend.o");
  expected_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/crtn.o");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc.a");
  expected_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc_eh.a");
  expected_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib/libc.a");

  EXPECT_EQ(expected_paths, input_paths);

  std::vector<string> expected_searchdirs;
  expected_searchdirs.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  expected_searchdirs.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.4.3");
  expected_searchdirs.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../../../lib");
  expected_searchdirs.push_back("/lib/../lib");
  expected_searchdirs.push_back("/usr/lib/../lib");
  expected_searchdirs.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/../../..");
  expected_searchdirs.push_back("/usr/lib/x86_64-linux-gnu");

  EXPECT_EQ(expected_searchdirs, searchdirs);

  EXPECT_EQ("", sysroot);
  EXPECT_EQ("", arch);
}
#endif
#ifdef __MACH__
TEST_F(LinkerInputProcessorTest, ParseMacClangDriverCommandLine) {
  string cwd = "/src";
  tmpdir_util_->SetCwd(cwd);
  tmpdir_util_->CreateTmpFile("/usr/lib/libSystem.dylib", kMachOFatFile);
  tmpdir_util_->CreateTmpFile("hello.o", kMachMagic);

  std::vector<string> args;
  // clang -### -o hello hello.o
  args.push_back("/usr/bin/ld");
  args.push_back("-demangle");
  args.push_back("-dynamic");
  args.push_back("-arch");
  args.push_back("x86_64");
  args.push_back("-macosx_version_min");
  args.push_back("10.8.0");
  args.push_back("-o");
  args.push_back("hello");
  args.push_back("hello.o");
  args.push_back("-lSystem");
  args.push_back("/usr/bin/../lib/clang/4.1/lib/darwin/libclang_rt.osx.a");

  std::vector<string> input_paths;
  std::vector<string> searchdirs;
  string sysroot;
  string arch;
  ParseDriverCommandLine(
      args, cwd, &sysroot, &arch, &searchdirs, &input_paths);
  std::vector<string> expected_paths;
  expected_paths.push_back("hello.o");
  expected_paths.push_back("/usr/bin/../lib/clang/4.1/lib/darwin/"
                           "libclang_rt.osx.a");
  expected_paths.push_back("/usr/lib/libSystem.dylib");
  EXPECT_EQ(expected_paths, input_paths);

  // searchdir should not have default ones.
  std::vector<string> expected_searchdirs;
  EXPECT_EQ(expected_searchdirs, searchdirs);

  EXPECT_EQ("", sysroot);
  EXPECT_EQ("x86_64", arch);
}
#endif

#ifdef __linux__
// TODO: fix library_path_resolver on mac could handle *.so for nacl.
TEST_F(LinkerInputProcessorTest, ParseNaclGccSolinkDriverCommandLine) {
  string cwd = "/src/chromium1/native_client/src/untrusted/nacl";
  tmpdir_util_->SetCwd(cwd);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/native_client/src/untrusted/nacl/"
      "../../../../out/Release/gen/tc_glibc/lib32/libimc_syscalls.so",
      kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/crti.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32/crtbeginS.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/native_client/src/untrusted/nacl/"
      "../../../../out/Release/obj/native_client/src/untrusted/nacl"
      "/imc_syscalls_lib.gen/glibc-x86-32-so/imc_syscalls_lib/imc_accept.o",
      kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libstdc++.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libm.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libc.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libgcc_s.so", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32/crtendS.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/crtn.o", kElfBinary);

  std::vector<string> args;
  args.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../libexec/gcc/x86_64-nacl/4.4.3/collect2");
  args.push_back("--no-add-needed");
  args.push_back("--eh-frame-hdr");
  args.push_back("--m");
  args.push_back("--elf_nacl");
  args.push_back("-shared");
  args.push_back("-o");
  args.push_back(
      "../../../../out/Release/gen/tc_glibc/lib32/libimc_syscalls.so");
  args.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/crti.o");
  args.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32/crtbeginS.o");
  args.push_back(
      "-L/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32");
  args.push_back(
      "-L/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32");
  args.push_back("-L../../../../out/Release/gen/tc_glibc/lib32");
  args.push_back(
      "-L/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3");
  args.push_back(
      "-L/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc");
  args.push_back(
      "-L/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl/lib");
  args.push_back("--as-needed");
  args.push_back(
      "../../../../out/Release/obj/native_client/src/untrusted/nacl"
      "/imc_syscalls_lib.gen/glibc-x86-32-so/imc_syscalls_lib/imc_accept.o");
  args.push_back("-soname");
  args.push_back("libimc_syscall.so");
  args.push_back("-lstdc++");
  args.push_back("-lm");
  args.push_back("-lgcc_s");
  args.push_back("-lc");
  args.push_back("-lgcc_s");
  args.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32/crtendS.o");
  args.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/crtn.o");

  std::vector<string> input_paths;
  std::vector<string> searchdirs;
  string sysroot;
  string arch;
  ParseDriverCommandLine(
      args, cwd, &sysroot, &arch, &searchdirs, &input_paths);
  std::vector<string> expected_paths;
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/crti.o");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32/crtbeginS.o");
  expected_paths.push_back(
      "../../../../out/Release/obj/native_client/src/untrusted/nacl"
      "/imc_syscalls_lib.gen/glibc-x86-32-so/imc_syscalls_lib/imc_accept.o");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32/crtendS.o");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/crtn.o");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libstdc++.so");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libm.so");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libgcc_s.so");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libc.so");
  expected_paths.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32/libgcc_s.so");
  EXPECT_EQ(expected_paths, input_paths);

  std::vector<string> expected_searchdirs;
  expected_searchdirs.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/32");
  expected_searchdirs.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl"
      "/lib/../lib32");
  expected_searchdirs.push_back(
      "../../../../out/Release/gen/tc_glibc/lib32");
  expected_searchdirs.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3");
  expected_searchdirs.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc");
  expected_searchdirs.push_back(
      "/src/chromium1/src/out/Release/gen/sdk/toolchain/linux_x86_glibc"
      "/bin/../lib/gcc/x86_64-nacl/4.4.3/../../../../x86_64-nacl/lib");
  EXPECT_EQ(expected_searchdirs, searchdirs);

  EXPECT_EQ("", sysroot);
  EXPECT_EQ("", arch);
}
#endif

TEST_F(LinkerInputProcessorTest, GetLibraryPath) {
  std::vector<string> envs;
  string cwd = "/dummy";
  tmpdir_util_->SetCwd(cwd);
  std::vector<string> searchdirs;
  std::vector<string> library_paths;

  searchdirs.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6");
  searchdirs.push_back("/usr/lib/x86_64-linux-gnu");
  envs.push_back(
      "COMPILER_PATH=../../third_party/gold/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/");
  envs.push_back(
      "LIBRARY_PATH="
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../lib/:"
      "/lib/x86_64-linux-gnu/:/lib/../lib/:/usr/lib/x86_64-linux-gnu/:"
      "/usr/lib/../lib/:/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../:/lib/:"
      "/usr/lib/");
  GetLibraryPath(envs, cwd, searchdirs, &library_paths);
  std::vector<string> expected_library_paths;
  expected_library_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6");
  expected_library_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../x86_64-linux-gnu");
  expected_library_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../../../lib");
  expected_library_paths.push_back("/lib/x86_64-linux-gnu");
  expected_library_paths.push_back("/lib/../lib");
  expected_library_paths.push_back("/usr/lib/x86_64-linux-gnu");
  expected_library_paths.push_back("/usr/lib/../lib");
  expected_library_paths.push_back(
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/../../..");
  expected_library_paths.push_back("/lib");
  expected_library_paths.push_back("/usr/lib");

  EXPECT_EQ(expected_library_paths, library_paths);
}

TEST_F(LinkerInputProcessorTest, GetLibraryPathNoLibraryPathEnv) {
  std::vector<string> envs;
  string cwd = "/dummy";
  tmpdir_util_->SetCwd(cwd);
  std::vector<string> searchdirs;
  std::vector<string> library_paths;

  searchdirs.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6");
  searchdirs.push_back("/usr/lib/x86_64-linux-gnu");
  envs.push_back(
      "COMPILER_PATH=../../third_party/gold/:"
      "/usr/lib/gcc/x86_64-linux-gnu/4.6/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/4.6/:"
      "/usr/lib/gcc/x86_64-linux-gnu/");
  GetLibraryPath(envs, cwd, searchdirs, &library_paths);
  std::vector<string> expected_library_paths;
  expected_library_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6");
  expected_library_paths.push_back("/usr/lib/x86_64-linux-gnu");

  EXPECT_EQ(expected_library_paths, library_paths);
}

TEST_F(LinkerInputProcessorTest, GetLibraryPathRelativePath) {
  // Not sure we will see this kind of pattern.
  std::vector<string> envs;
  string cwd = "/dummy";
  tmpdir_util_->SetCwd(cwd);
  std::vector<string> searchdirs;
  std::vector<string> library_paths;

  envs.push_back("LIBRARY_PATH=../../third_party/gold/:"
                 "/usr/lib/gcc/x86_64-linux-gnu/4.6/");
  GetLibraryPath(envs, cwd, searchdirs, &library_paths);
  std::vector<string> expected_library_paths;
  expected_library_paths.push_back("/dummy/../../third_party/gold");
  expected_library_paths.push_back("/usr/lib/gcc/x86_64-linux-gnu/4.6");

  EXPECT_EQ(expected_library_paths, library_paths);
}

TEST_F(LinkerInputProcessorTest, CheckFileType) {
#ifndef _WIN32
  tmpdir_util_->CreateTmpFile("/lib64/ld-linux-x86-64.so.2", kElfBinary);
  EXPECT_EQ(LinkerInputProcessor::ELF_BINARY_FILE,
            CheckFileType("/lib64/ld-linux-x86-64.so.2"));
  tmpdir_util_->CreateTmpFile(
      "/src/out/Release/obj.target/chrome/chrome/app/chrome_main.o",
      kElfBinary);
  EXPECT_EQ(LinkerInputProcessor::ELF_BINARY_FILE,
            CheckFileType(
                "/src/out/Release/obj.target/chrome/chrome/app/chrome_main.o"));
  tmpdir_util_->CreateTmpFile(
      "/src/out/Release/obj.target/seccompsandbox/libseccomp_sandbox.a",
      kThinArFile);
  EXPECT_EQ(LinkerInputProcessor::THIN_ARCHIVE_FILE,
            CheckFileType("/src/out/Release/obj.target/"
                          "seccompsandbox/libseccomp_sandbox.a"));
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc.a", kArFile);
  EXPECT_EQ(LinkerInputProcessor::ARCHIVE_FILE,
            CheckFileType("/usr/lib/gcc/x86_64-linux-gnu/4.4.3/libgcc.a"));
  tmpdir_util_->CreateTmpFile(
      "/usr/lib/libc.so", "OUTPUT_FORMAT(elf64-x86-64)");
  EXPECT_EQ(LinkerInputProcessor::OTHER_FILE,
            CheckFileType("/usr/lib/libc.so"));
#else
  tmpdir_util_->CreateTmpFile("\\lib64\\elf.o", kElfBinary);
  EXPECT_EQ(LinkerInputProcessor::ELF_BINARY_FILE,
            CheckFileType("\\lib64\\elf.o"));
  tmpdir_util_->CreateTmpFile("\\out\\Debug\\thinar.a", kThinArFile);
  EXPECT_EQ(LinkerInputProcessor::THIN_ARCHIVE_FILE,
            CheckFileType("\\out\\Debug\\thinar.a"));
  tmpdir_util_->CreateTmpFile("\\out\\Debug\\ar.a", kArFile);
  EXPECT_EQ(LinkerInputProcessor::ARCHIVE_FILE,
            CheckFileType("\\out\\Debug\\ar.a"));
  tmpdir_util_->CreateTmpFile("\\lib\\libc.so", "OUTPUT_FORMAT(elf64-x86-64)");
  EXPECT_EQ(LinkerInputProcessor::OTHER_FILE,
            CheckFileType("\\lib\\libc.so"));
#endif
#ifdef __MACH__
  tmpdir_util_->CreateTmpFile("/usr/lib/libSystem.dylib", kMachOFatFile);
  EXPECT_EQ(LinkerInputProcessor::MACHO_FAT_FILE,
            CheckFileType("/usr/lib/libSystem.dylib"));
  tmpdir_util_->CreateTmpFile("magic.o", kMachMagic);
  EXPECT_EQ(LinkerInputProcessor::MACHO_OBJECT_FILE,
            CheckFileType("magic.o"));
  tmpdir_util_->CreateTmpFile("cigam.o", kMachCigam);
  EXPECT_EQ(LinkerInputProcessor::MACHO_OBJECT_FILE,
            CheckFileType("cigam.o"));
  tmpdir_util_->CreateTmpFile("magic64.o", kMachMagic64);
  EXPECT_EQ(LinkerInputProcessor::MACHO_OBJECT_FILE,
            CheckFileType("magic64.o"));
  tmpdir_util_->CreateTmpFile("cigam64.o", kMachCigam64);
  EXPECT_EQ(LinkerInputProcessor::MACHO_OBJECT_FILE,
            CheckFileType("cigam64.o"));
#endif
}

#ifdef __linux__
// TODO: investigate reason why this fails.
TEST_F(LinkerInputProcessorTest, ParseThinArchive) {
  tmpdir_util_->CreateTmpFile(
      "/src/out/Release/obj.target/foo/foo.o", kElfBinary);
  tmpdir_util_->CreateTmpFile(
      "/src/out/Release/obj.target/foo/bar.o", kElfBinary);
  std::vector<string> files;
  files.push_back("../foo/foo.o");
  files.push_back("../foo/bar.o");
  Archive("/src/out/Release/obj.target/bar", "rcuT", "libfoo.a", files);

  std::set<string> input_files;
  ParseThinArchive("/src/out/Release/obj.target/bar/libfoo.a", &input_files);
  std::set<string> expected_files;
  expected_files.insert("/src/out/Release/obj.target/bar/../foo/foo.o");
  expected_files.insert("/src/out/Release/obj.target/bar/../foo/bar.o");
  EXPECT_EQ(expected_files, input_files);
}
#endif

}  // namespace devtools_goma
