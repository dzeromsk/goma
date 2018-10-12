// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/path_resolver.h"

#include "gtest/gtest.h"

namespace devtools_goma {

class PathResolverTest : public ::testing::Test {
};

TEST_F(PathResolverTest, PlatformConvertCommon) {
  EXPECT_EQ(
      PathResolver::PlatformConvert(
          "/FoO/BaR", PathResolver::kWin32PathSep, PathResolver::kPreserveCase),
      "\\FoO\\BaR");
  EXPECT_EQ(
      PathResolver::PlatformConvert(
          "\\FoO\\BaR", PathResolver::kWin32PathSep,
          PathResolver::kPreserveCase),
      "\\FoO\\BaR");
  EXPECT_EQ(
      PathResolver::PlatformConvert(
          "/FoO/BaR", PathResolver::kWin32PathSep, PathResolver::kLowerCase),
      "\\foo\\bar");
  EXPECT_EQ(
      PathResolver::PlatformConvert(
          "\\FoO\\BaR", PathResolver::kWin32PathSep, PathResolver::kLowerCase),
      "\\foo\\bar");
}

#ifdef _WIN32
TEST_F(PathResolverTest, PlatformConvertWin32) {
  EXPECT_EQ(PathResolver::PlatformConvert("/FoO/BaR"), "\\FoO\\BaR");
  EXPECT_EQ(PathResolver::PlatformConvert("C:\\FoO/BaR"), "C:\\FoO\\BaR");
  // Note: kPosixPathSep is not implemented for Windows.
}
#else
TEST_F(PathResolverTest, PlatformConvertPOSIX) {
  EXPECT_EQ(PathResolver::PlatformConvert("/FoO/BaR"), "/FoO/BaR");
  EXPECT_EQ(PathResolver::PlatformConvert("\\FoO\\BaR"), "/FoO/BaR");
  EXPECT_EQ(
      PathResolver::PlatformConvert(
          "/FoO/BaR", PathResolver::kPosixPathSep, PathResolver::kLowerCase),
      "/foo/bar");
  EXPECT_EQ(
      PathResolver::PlatformConvert(
          "\\FoO\\BaR", PathResolver::kPosixPathSep, PathResolver::kLowerCase),
      "/foo/bar");
}
#endif

TEST_F(PathResolverTest, ResolvePath) {
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/bar"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/./foo/bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/./bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/bar/."));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/././foo/bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/./././foo/./bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/./foo/././bar"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/../foo/bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/../../foo/bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/../../../foo/bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/bar"),
      PathResolver::ResolvePath("/foo/../bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo"),
      PathResolver::ResolvePath("/foo/bar/../"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/baz/../foo/bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/bar"),
      PathResolver::ResolvePath("/baz/../../foo/../bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/"),
      PathResolver::ResolvePath("/baz/../../foo/../bar/../"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/baz/../bar"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/baz/quux/../../bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/baz/../quux/../bar"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/baz//////../quux/../bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/baz//../quux/////..////////bar"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/baz"),
      PathResolver::ResolvePath("/../../../foo/../../../baz"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/bar/baz/.."));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/foo/bar"),
      PathResolver::ResolvePath("/foo/bar/baz/../"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("/"),
      PathResolver::ResolvePath("/"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("../.."),
      PathResolver::ResolvePath("././../.."));
  EXPECT_EQ(
      PathResolver::PlatformConvert("../.."),
      PathResolver::ResolvePath("./.././.."));

#ifndef _WIN32
  EXPECT_EQ("/foo/bar", PathResolver::ResolvePath("//foo//bar"));
#else
  EXPECT_EQ("C:\\foo\\bar", PathResolver::ResolvePath("C:\\foo\\bar"));
  EXPECT_EQ("C:\\foo\\bar", PathResolver::ResolvePath("C:\\.\\foo\\bar"));
  EXPECT_EQ("C:\\foo\\bar", PathResolver::ResolvePath("C:\\foo\\.\\bar"));
  EXPECT_EQ("C:\\foo\\bar", PathResolver::ResolvePath("C:\\foo\\bar\\."));
  EXPECT_EQ("C:\\foo\\bar", PathResolver::ResolvePath("C:\\..\\foo\\bar"));
  EXPECT_EQ("C:\\foo\\bar",
            PathResolver::ResolvePath("C:\\..\\..\\foo\\bar"));
  EXPECT_EQ("C:\\foo\\bar",
            PathResolver::ResolvePath("C:\\baz\\..\\foo\\bar"));
  EXPECT_EQ("C:\\foo\\bar",
            PathResolver::ResolvePath("C:\\foo\\baz\\..\\bar"));
  EXPECT_EQ("C:\\foo\\bar",
            PathResolver::ResolvePath("C:\\foo\\baz\\quux\\..\\..\\bar"));
  EXPECT_EQ("C:\\foo\\bar",
            PathResolver::ResolvePath("C:\\foo\\baz\\..\\quux\\..\\bar"));
  EXPECT_EQ("C:\\foo\\bar", PathResolver::ResolvePath("C:\\foo\\bar\\baz\\.."));
  EXPECT_EQ("C:\\foo\\bar",
            PathResolver::ResolvePath("C:\\foo\\bar\\baz\\..\\"));
  EXPECT_EQ("\\\\foo\\bar", PathResolver::ResolvePath("\\\\foo\\bar"));
  EXPECT_EQ("\\\\foo\\bar", PathResolver::ResolvePath("\\\\foo\\.\\bar"));
  EXPECT_EQ("\\\\foo\\bar", PathResolver::ResolvePath("\\\\foo\\bar\\."));
  EXPECT_EQ("\\\\foo\\bar", PathResolver::ResolvePath("\\\\foo\\..\\bar"));
  EXPECT_EQ("\\\\foo\\bar", PathResolver::ResolvePath("\\\\foo\\..\\..\\bar"));
  EXPECT_EQ("\\\\baz\\foo\\bar",
            PathResolver::ResolvePath("\\\\baz\\..\\foo\\bar"));
  EXPECT_EQ("\\\\foo\\bar", PathResolver::ResolvePath("\\\\foo\\baz\\..\\bar"));
  EXPECT_EQ("\\\\foo\\bar",
            PathResolver::ResolvePath("\\\\foo\\baz\\quux\\..\\..\\bar"));
  EXPECT_EQ("\\\\foo\\bar",
            PathResolver::ResolvePath("\\\\foo\\baz\\..\\quux\\..\\bar"));
  EXPECT_EQ("\\\\foo\\bar", PathResolver::ResolvePath("\\\\foo\\bar\\baz\\.."));
  EXPECT_EQ("\\\\foo\\bar",
            PathResolver::ResolvePath("\\\\foo\\bar\\baz\\..\\"));
#endif

  EXPECT_EQ(
      PathResolver::PlatformConvert("relative/path/name"),
      PathResolver::ResolvePath("./relative/path/name"));

  EXPECT_EQ(
      PathResolver::PlatformConvert("path/name"),
      PathResolver::ResolvePath("relative/../path/name"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("../full/path/name"),
      PathResolver::ResolvePath("../full/path/name"));
  EXPECT_EQ(
      PathResolver::PlatformConvert("/full/path/name"),
      PathResolver::ResolvePath("/../full/path/name"));
}

TEST_F(PathResolverTest, WeakReletivePath) {
  EXPECT_EQ("foo", PathResolver::WeakRelativePath("/tmp/foo", "/tmp"));
  EXPECT_EQ("foo/bar",
            PathResolver::WeakRelativePath("/tmp/foo/bar", "/tmp"));
  EXPECT_EQ("bar", PathResolver::WeakRelativePath("/tmp/foo/bar", "/tmp/foo"));
  EXPECT_EQ("foo/../bar",
            PathResolver::WeakRelativePath("/tmp/foo/../bar", "/tmp"));
  EXPECT_EQ("../foo",
            PathResolver::WeakRelativePath("/tmp/foo", "/tmp/baz"));
  EXPECT_EQ("../../foo",
            PathResolver::WeakRelativePath("/tmp/foo", "/tmp/bar/baz"));
  EXPECT_EQ("../foo",
            PathResolver::WeakRelativePath("/tmp/foo", "/tmp/foobar"));
  EXPECT_EQ("../foobar",
            PathResolver::WeakRelativePath("/tmp/foobar", "/tmp/foo"));
  EXPECT_EQ("/usr/include",
            PathResolver::WeakRelativePath("/usr/include", "/tmp"));

  // Windows path.
  EXPECT_EQ("foo", PathResolver::WeakRelativePath("C:\\tmp\\foo", "C:\\tmp"));
  EXPECT_EQ(
      "foo\\bar",
      PathResolver::WeakRelativePath("C:\\tmp\\foo\\bar", "C:\\tmp"));
  EXPECT_EQ(
      "bar",
      PathResolver::WeakRelativePath("C:\\tmp\\foo\\bar", "C:\\tmp\\foo"));
  EXPECT_EQ(
      "foo\\..\\bar",
      PathResolver::WeakRelativePath("C:\\tmp\\foo\\..\\bar", "C:\\tmp"));
  EXPECT_EQ(
      "..\\foo",
      PathResolver::WeakRelativePath("C:\\tmp\\foo", "C:\\tmp\\baz"));
  EXPECT_EQ(
      "..\\..\\foo",
      PathResolver::WeakRelativePath("C:\\tmp\\foo", "C:\\tmp\\bar\\baz"));
  EXPECT_EQ(
      "..\\foo",
      PathResolver::WeakRelativePath("C:\\tmp\\foo", "C:\\tmp\\foobar"));
  EXPECT_EQ(
      "..\\foobar",
      PathResolver::WeakRelativePath("C:\\tmp\\foobar", "C:\\tmp\\foo"));
  EXPECT_EQ(
      "C:\\usr\\include",
      PathResolver::WeakRelativePath("C:\\usr\\include", "C:\\tmp"));
  EXPECT_EQ(
      "C:\\usr\\include",
      PathResolver::WeakRelativePath("C:\\usr\\include", "D:\\usr\\include"));
  EXPECT_EQ(
      "C:\\usr\\include",
      PathResolver::WeakRelativePath("C:\\usr\\include", "\\usr\\include"));
  EXPECT_EQ(
      "foo", PathResolver::WeakRelativePath("\\\\g\\tmp\\foo", "\\\\g\\tmp"));
  EXPECT_EQ(
      "foo\\bar",
      PathResolver::WeakRelativePath("\\\\g\\tmp\\foo\\bar", "\\\\g\\tmp"));
  EXPECT_EQ(
      "bar",
      PathResolver::WeakRelativePath("\\\\g\\tmp\\foo\\bar",
                                     "\\\\g\\tmp\\foo"));
  EXPECT_EQ(
      "foo\\..\\bar",
      PathResolver::WeakRelativePath("\\\\g\\tmp\\foo\\..\\bar", "\\\\g\\tmp"));
  EXPECT_EQ(
      "..\\foo",
      PathResolver::WeakRelativePath("\\\\g\\tmp\\foo", "\\\\g\\tmp\\baz"));
  EXPECT_EQ(
      "..\\..\\foo",
      PathResolver::WeakRelativePath("\\\\g\\tmp\\foo",
                                     "\\\\g\\tmp\\bar\\baz"));
  EXPECT_EQ(
      "..\\foo",
      PathResolver::WeakRelativePath("\\\\g\\tmp\\foo", "\\\\g\\tmp\\foobar"));
  EXPECT_EQ(
      "..\\foobar",
      PathResolver::WeakRelativePath("\\\\g\\tmp\\foobar", "\\\\g\\tmp\\foo"));
  EXPECT_EQ(
      "\\\\g\\usr\\include",
      PathResolver::WeakRelativePath("\\\\g\\usr\\include", "\\\\g\\tmp"));
  EXPECT_EQ(
      "\\\\g\\usr\\include",
      PathResolver::WeakRelativePath("\\\\g\\usr\\include",
                                     "\\\\gg\\usr\\include"));
  EXPECT_EQ(
      "\\\\g\\usr\\include",
      PathResolver::WeakRelativePath("\\\\g\\usr\\include", "\\usr\\include"));
  EXPECT_EQ(
      "d:foo.obj", PathResolver::WeakRelativePath("d:foo.obj", "C:\\tmp"));
}

TEST_F(PathResolverTest, SystemPath) {
  PathResolver pr;
  pr.RegisterSystemPath("/usr/include");
  pr.RegisterSystemPath("/usr/include/c++/4.4");
  EXPECT_TRUE(pr.IsSystemPath("/usr/include"));
  EXPECT_TRUE(pr.IsSystemPath("/usr/include/c++/4.4"));
  EXPECT_TRUE(pr.IsSystemPath("/usr/include/cairo"));
  EXPECT_TRUE(pr.IsSystemPath("/usr/include/gtk-2.0"));
  EXPECT_FALSE(pr.IsSystemPath("/home/goma/src"));
  EXPECT_FALSE(pr.IsSystemPath("/var/tmp"));
}

#ifdef _WIN32
TEST_F(PathResolverTest, SystemPathWin32) {
  PathResolver pr;
  pr.RegisterSystemPath("C:\\Windows");
  pr.RegisterSystemPath("C:\\Windows\\System32");
  pr.RegisterSystemPath("C:\\Program Files");
  pr.RegisterSystemPath("C:\\Program Files (x86)");
  EXPECT_TRUE(pr.IsSystemPath("C:\\Windows\\write.exe"));
  EXPECT_TRUE(pr.IsSystemPath("C:\\Windows\\System32\\cmd.exe"));
  EXPECT_TRUE(pr.IsSystemPath("C:\\Program Files\\Internet Explorer\\IE.DLL"));
  EXPECT_TRUE(pr.IsSystemPath("C:\\Program Files (x86)\\Adobe\\acrobat.exe"));
  EXPECT_FALSE(pr.IsSystemPath("C:\\ProgramData"));
  EXPECT_FALSE(pr.IsSystemPath("D:\\Program Files"));
}
#endif

}  // namespace devtools_goma
