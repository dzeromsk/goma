// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "library_path_resolver.h"

#ifdef _WIN32
# include "config_win.h"
# include <shlobj.h>
#else
# include <limits.h>
#endif

#include <memory>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "path.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

class LibraryPathResolverTest : public testing::Test {
 public:
  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("library_path_resolver_test");

    // |tmpdir_| should be kept for LibraryPathResolver::fakeroot_.
    tmpdir_ = tmpdir_util_->tmpdir();
    cwd_ = tmpdir_util_->cwd();
    LibraryPathResolver::fakeroot_ = tmpdir_.c_str();
  }

  void TearDown() override {
    LibraryPathResolver::fakeroot_ = "";
    tmpdir_util_.reset();
  }

 protected:
  std::unique_ptr<TmpdirUtil> tmpdir_util_;

  std::string tmpdir_;
  std::string cwd_;
};

TEST_F(LibraryPathResolverTest, SimpleTest) {
#ifdef __linux__
  tmpdir_util_->CreateEmptyFile("/usr/lib/libX11.a");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libX11.so");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libc.so");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libc.a");
  tmpdir_util_->CreateEmptyFile("/lib/libc.so.6");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libgcc_s.a");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libgcc.so");
  tmpdir_util_->CreateEmptyFile("/usr/local/lib/libX11.so");
  tmpdir_util_->CreateEmptyFile("/usr/local/lib/libglib.so");

  std::vector<string> search_dirs;
  search_dirs.push_back("/lib");
  search_dirs.push_back("/usr/lib");
  LibraryPathResolver library_path_resolver(cwd_);
  library_path_resolver.AppendSearchdirs(search_dirs);
  EXPECT_EQ("/usr/lib/libX11.so",
            library_path_resolver.ExpandLibraryPath("X11"));
  EXPECT_EQ("/usr/lib/libc.so",
            library_path_resolver.ExpandLibraryPath("c"));
  EXPECT_EQ("/usr/lib/libgcc_s.a",
            library_path_resolver.ExpandLibraryPath("gcc_s"));
  EXPECT_EQ("/usr/lib/libgcc.so",
            library_path_resolver.ExpandLibraryPath("gcc"));
  EXPECT_EQ("", library_path_resolver.ExpandLibraryPath("glib"));

  library_path_resolver.AddSearchdir("/usr/local/lib");
  EXPECT_EQ("/usr/lib/libX11.so",
            library_path_resolver.ExpandLibraryPath("X11"));
  EXPECT_EQ("/usr/local/lib/libglib.so",
            library_path_resolver.ExpandLibraryPath("glib"));

  EXPECT_EQ("/lib/libc.so.6",
            library_path_resolver.FindBySoname("libc.so.6"));
#elif defined(__MACH__)
  tmpdir_util_->CreateEmptyFile("/usr/lib/libSystem.dylib");
  tmpdir_util_->CreateEmptyFile("/usr/lib/liby.a");
  tmpdir_util_->CreateEmptyFile("/usr/lib/crt1.10.6.o");
  tmpdir_util_->CreateEmptyFile("/usr/local/lib/libdummy.dylib");
  tmpdir_util_->CreateEmptyFile("/this/is/test/dir/libdummy2.dylib");
  tmpdir_util_->CreateEmptyFile("/yet/another/dir/libdummy3.dylib");

  // /usr/lib and /usr/local/lib are default search path.
  std::vector<string> search_dirs;
  search_dirs.push_back("/this/is/test/dir");
  LibraryPathResolver library_path_resolver(cwd_);
  library_path_resolver.AppendSearchdirs(search_dirs);

  EXPECT_EQ("/usr/lib/libSystem.dylib",
            library_path_resolver.ExpandLibraryPath("System"));
  EXPECT_EQ("/usr/lib/liby.a",
            library_path_resolver.ExpandLibraryPath("y"));
  EXPECT_EQ("/usr/local/lib/libdummy.dylib",
            library_path_resolver.ExpandLibraryPath("dummy"));
  EXPECT_EQ("/this/is/test/dir/libdummy2.dylib",
            library_path_resolver.ExpandLibraryPath("dummy2"));

  library_path_resolver.AddSearchdir("/yet/another/dir");
  EXPECT_EQ("/usr/lib/libSystem.dylib",
            library_path_resolver.ExpandLibraryPath("System"));
  EXPECT_EQ("/yet/another/dir/libdummy3.dylib",
            library_path_resolver.ExpandLibraryPath("dummy3"));

  EXPECT_EQ("/usr/lib/crt1.10.6.o",
            library_path_resolver.FindBySoname("crt1.10.6.o"));
#elif defined(_WIN32)
  tmpdir_util_->CreateEmptyFile("\\vs9\\vc\\lib\\libcmtd.lib");
  tmpdir_util_->CreateEmptyFile("\\vs9\\vc\\lib\\msvcprt.lib");
  tmpdir_util_->CreateEmptyFile(
      "\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\Lib\\msxml2.lib");
  tmpdir_util_->CreateEmptyFile("\\vs10\\vc\\lib\\libcmtd.lib");
  std::vector<string> search_dirs;
  search_dirs.push_back("\\vs9\\vc\\lib");
  search_dirs.push_back("\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\Lib");
  LibraryPathResolver library_path_resolver(cwd_);
  library_path_resolver.AppendSearchdirs(search_dirs);
  EXPECT_EQ("\\vs9\\vc\\lib\\libcmtd.lib",
            library_path_resolver.ExpandLibraryPath("libcmtd.lib"));
  EXPECT_EQ("\\vs9\\vc\\lib\\msvcprt.lib",
            library_path_resolver.ExpandLibraryPath("msvcprt.lib"));
  EXPECT_EQ("\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\Lib\\msxml2.lib",
            library_path_resolver.ExpandLibraryPath("msxml2.lib"));
  library_path_resolver.AddSearchdir("\\vs10\\vc\\lib");
  EXPECT_EQ("\\vs9\\vc\\lib\\libcmtd.lib",
            library_path_resolver.ExpandLibraryPath("libcmtd.lib"));
#endif
}

#ifdef __linux__
TEST_F(LibraryPathResolverTest, SimpleTestStatic) {
  tmpdir_util_->CreateEmptyFile("/usr/lib/libX11.a");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libX11.so");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libc.so");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libc.a");
  tmpdir_util_->CreateEmptyFile("/lib/libc.so.6");
  tmpdir_util_->CreateEmptyFile("/usr/lib/libgcc_s.a");
  tmpdir_util_->CreateEmptyFile("/usr/local/lib/libX11.so");
  tmpdir_util_->CreateEmptyFile("/usr/local/lib/libX11.a");
  tmpdir_util_->CreateEmptyFile("/usr/local/lib/libglib.so");
  tmpdir_util_->CreateEmptyFile("/usr/local/lib/libglib.a");

  std::vector<string> search_dirs;
  search_dirs.push_back("/lib");
  search_dirs.push_back("/usr/lib");
  LibraryPathResolver library_path_resolver(cwd_);
  library_path_resolver.AppendSearchdirs(search_dirs);
  library_path_resolver.PreventSharedLibrary();
  EXPECT_EQ("/usr/lib/libX11.a",
            library_path_resolver.ExpandLibraryPath("X11"));
  EXPECT_EQ("/usr/lib/libc.a",
            library_path_resolver.ExpandLibraryPath("c"));
  EXPECT_EQ("/usr/lib/libgcc_s.a",
            library_path_resolver.ExpandLibraryPath("gcc_s"));
  EXPECT_EQ("", library_path_resolver.ExpandLibraryPath("glib"));
  library_path_resolver.AddSearchdir("/usr/local/lib");
  EXPECT_EQ("/usr/lib/libX11.a",
            library_path_resolver.ExpandLibraryPath("X11"));
  EXPECT_EQ("/usr/local/lib/libglib.a",
            library_path_resolver.ExpandLibraryPath("glib"));
}
#endif

#ifdef __MACH__
TEST_F(LibraryPathResolverTest, SimpleTestSyslibroot) {
  tmpdir_util_->CreateEmptyFile("/usr/lib/libSystem.dylib");
  tmpdir_util_->CreateEmptyFile("/Developer/SDKs/MacOSX10.6.sdk"
                                "/usr/lib/libSystem.dylib");
  tmpdir_util_->CreateEmptyFile("lib/libtest.dylib");
  LibraryPathResolver library_path_resolver(cwd_);
  EXPECT_EQ("/usr/lib/libSystem.dylib",
            library_path_resolver.ExpandLibraryPath("System"));
  EXPECT_EQ("", library_path_resolver.ExpandLibraryPath("test"));

  library_path_resolver.AddSearchdir("lib");
  EXPECT_EQ("/usr/lib/libSystem.dylib",
            library_path_resolver.ExpandLibraryPath("System"));
  EXPECT_EQ(file::JoinPath(cwd_, "lib/libtest.dylib"),
            library_path_resolver.ExpandLibraryPath("test"));

  library_path_resolver.SetSyslibroot("/Developer/SDKs/MacOSX10.6.sdk");
  EXPECT_EQ("/Developer/SDKs/MacOSX10.6.sdk/usr/lib/libSystem.dylib",
            library_path_resolver.ExpandLibraryPath("System"));
  EXPECT_EQ(file::JoinPath(cwd_, "lib/libtest.dylib"),
            library_path_resolver.ExpandLibraryPath("test"));
}
#endif

}  // namespace devtools_goma
