// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "path_util.h"

#include <gtest/gtest.h>

namespace devtools_goma {

TEST(PathUtilTest, IsPosixAbsolutePath) {
  EXPECT_TRUE(IsPosixAbsolutePath("/"));
  EXPECT_TRUE(IsPosixAbsolutePath("/foo"));
  EXPECT_TRUE(IsPosixAbsolutePath("/foo/bar"));
  EXPECT_TRUE(IsPosixAbsolutePath("/../foo"));
  EXPECT_TRUE(IsPosixAbsolutePath("/foo/../bar"));

  EXPECT_FALSE(IsPosixAbsolutePath("."));
  EXPECT_FALSE(IsPosixAbsolutePath(".."));
  EXPECT_FALSE(IsPosixAbsolutePath("foo"));
  EXPECT_FALSE(IsPosixAbsolutePath("foo/bar"));
  EXPECT_FALSE(IsPosixAbsolutePath("../foo"));

  EXPECT_FALSE(IsPosixAbsolutePath("c:\\Users\\foo"));
  EXPECT_FALSE(IsPosixAbsolutePath("\\\\Host\\dir\\content"));
}

TEST(PathUtilTest, IsWindowsAbsolutePath) {
  EXPECT_TRUE(IsWindowsAbsolutePath("c:\\"));
  EXPECT_TRUE(IsWindowsAbsolutePath("C:\\"));
  EXPECT_TRUE(IsWindowsAbsolutePath("c:/"));
  EXPECT_TRUE(IsWindowsAbsolutePath("C:/"));
  EXPECT_TRUE(IsWindowsAbsolutePath("c:\\Users\\foo"));
  EXPECT_TRUE(IsWindowsAbsolutePath("c:/Users/foo"));
  EXPECT_TRUE(IsWindowsAbsolutePath("c:\\Users/foo"));
  EXPECT_TRUE(IsWindowsAbsolutePath("c:/Users\\foo"));

  EXPECT_TRUE(IsWindowsAbsolutePath("\\\\Host\\"));
  EXPECT_TRUE(IsWindowsAbsolutePath("\\\\Host\\dir"));
  EXPECT_TRUE(IsWindowsAbsolutePath("\\\\Host\\dir\\content"));

  EXPECT_FALSE(IsWindowsAbsolutePath("/"));
  EXPECT_FALSE(IsWindowsAbsolutePath("/foo"));
  EXPECT_FALSE(IsWindowsAbsolutePath("/foo/bar"));
  EXPECT_FALSE(IsWindowsAbsolutePath("/../foo"));
  EXPECT_FALSE(IsWindowsAbsolutePath("/foo/../bar"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\foo"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\foo\\bar"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\..\\foo"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\foo\\..\\bar"));

  EXPECT_FALSE(IsWindowsAbsolutePath("."));
  EXPECT_FALSE(IsWindowsAbsolutePath(".."));
  EXPECT_FALSE(IsWindowsAbsolutePath("foo"));
  EXPECT_FALSE(IsWindowsAbsolutePath("foo/bar"));
  EXPECT_FALSE(IsWindowsAbsolutePath("../foo"));

  // TODO: check wheather followings is allowed or not.
  EXPECT_FALSE(IsWindowsAbsolutePath("c:"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\\\host"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\\\Host\\dir/content"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\\\Host/dir\\content"));
  EXPECT_FALSE(IsWindowsAbsolutePath("\\\\Host/dir/content"));
}

TEST(PathUtilTest, HasPrefixDirWithSep) {
  EXPECT_TRUE(HasPrefixDirWithSep("/home/foo/bar", "/home/foo", '/'));
  EXPECT_TRUE(HasPrefixDirWithSep("/home/foo", "/home/foo", '/'));
  EXPECT_TRUE(HasPrefixDirWithSep("/home/foo/", "/home/foo", '/'));

  EXPECT_FALSE(HasPrefixDirWithSep("/foo", "/baz", '/'));
  EXPECT_FALSE(HasPrefixDirWithSep("/foo/bar", "/bar", '/'));
  EXPECT_FALSE(HasPrefixDirWithSep("/foo", "/bar/baz", '/'));
  EXPECT_FALSE(HasPrefixDirWithSep("/foo", "/foo/bar", '/'));
  EXPECT_FALSE(HasPrefixDirWithSep("/home/foobar", "/home/foo", '/'));

  EXPECT_TRUE(HasPrefixDirWithSep("home/foo", "home/foo", '/'));
  EXPECT_TRUE(HasPrefixDirWithSep("home/foo/bar", "home/foo", '/'));

  EXPECT_TRUE(HasPrefixDirWithSep("../home/foo", "../home/foo", '/'));
  EXPECT_TRUE(HasPrefixDirWithSep("../home/foo/bar", "../home/foo", '/'));

  EXPECT_TRUE(HasPrefixDirWithSep("c:\\home\\foo\\bar", "c:\\home\\foo", '\\'));
  EXPECT_TRUE(HasPrefixDirWithSep("c:\\home\\foo", "c:\\home\\foo", '\\'));
  EXPECT_TRUE(HasPrefixDirWithSep("c:\\home\\foo\\", "c:\\home\\foo", '\\'));

  EXPECT_FALSE(HasPrefixDirWithSep("c:\\foo", "c:\\baz", '\\'));
  EXPECT_FALSE(HasPrefixDirWithSep("c:\\foo\\bar", "c:\\bar", '\\'));
  EXPECT_FALSE(HasPrefixDirWithSep("c:\\foo", "c:\\bar\\baz", '\\'));
  EXPECT_FALSE(HasPrefixDirWithSep("c:\\foo", "c:\\foo\\bar", '\\'));
  EXPECT_FALSE(HasPrefixDirWithSep("c:\\home\\foobar", "c:\\home\\foo", '\\'));

  EXPECT_TRUE(HasPrefixDirWithSep("home\\foo", "home\\foo", '\\'));
  EXPECT_TRUE(HasPrefixDirWithSep("home\\foo\\bar", "home\\foo", '\\'));

  EXPECT_TRUE(HasPrefixDirWithSep("..\\home\\foo", "..\\home\\foo", '\\'));
  EXPECT_TRUE(HasPrefixDirWithSep("..\\home\\foo\\bar", "..\\home\\foo", '\\'));
}

TEST(PathUtilTest, HasPrefixDir) {
  EXPECT_TRUE(HasPrefixDir("/home/foo/bar", "/home/foo"));
  EXPECT_TRUE(HasPrefixDir("/home/foo", "/home/foo"));
  EXPECT_TRUE(HasPrefixDir("/home/foo/", "/home/foo"));

  EXPECT_FALSE(HasPrefixDir("/foo", "/baz"));
  EXPECT_FALSE(HasPrefixDir("/foo/bar", "/bar"));
  EXPECT_FALSE(HasPrefixDir("/foo", "/bar/baz"));
  EXPECT_FALSE(HasPrefixDir("/foo", "/foo/bar"));
  EXPECT_FALSE(HasPrefixDir("/home/foobar", "/home/foo"));

  EXPECT_TRUE(HasPrefixDir("home/foo", "home/foo"));
  EXPECT_TRUE(HasPrefixDir("home/foo/bar", "home/foo"));

  EXPECT_TRUE(HasPrefixDir("../home/foo", "../home/foo"));
  EXPECT_TRUE(HasPrefixDir("../home/foo/bar", "../home/foo"));

#ifdef _WIN32
  EXPECT_TRUE(HasPrefixDir("c:\\home\\foo\\bar", "c:\\home\\foo"));
  EXPECT_TRUE(HasPrefixDir("c:\\home\\foo", "c:\\home\\foo"));
  EXPECT_TRUE(HasPrefixDir("c:\\home\\foo\\", "c:\\home\\foo"));

  EXPECT_FALSE(HasPrefixDir("c:\\foo", "c:\\baz"));
  EXPECT_FALSE(HasPrefixDir("c:\\foo\\bar", "c:\\bar"));
  EXPECT_FALSE(HasPrefixDir("c:\\foo", "c:\\bar\\baz"));
  EXPECT_FALSE(HasPrefixDir("c:\\foo", "c:\\foo\\bar"));
  EXPECT_FALSE(HasPrefixDir("c:\\home\\foobar", "c:\\home\\foo"));

  EXPECT_TRUE(HasPrefixDir("home\\foo", "home\\foo"));
  EXPECT_TRUE(HasPrefixDir("home\\foo\\bar", "home\\foo"));

  EXPECT_TRUE(HasPrefixDir("..\\home\\foo", "..\\home\\foo"));
  EXPECT_TRUE(HasPrefixDir("..\\home\\foo\\bar", "..\\home\\foo"));

  EXPECT_TRUE(HasPrefixDir("c:/home/foo/bar", "c:/home/foo"));
  EXPECT_TRUE(HasPrefixDir("c:/home/foo", "c:/home/foo"));
  EXPECT_TRUE(HasPrefixDir("c:/home/foo/", "c:/home/foo"));

  EXPECT_FALSE(HasPrefixDir("c:/foo", "c:/baz"));
  EXPECT_FALSE(HasPrefixDir("c:/foo/bar", "c:/bar"));
  EXPECT_FALSE(HasPrefixDir("c:/foo", "c:/bar/baz"));
  EXPECT_FALSE(HasPrefixDir("c:/foo", "c:/foo/bar"));
  EXPECT_FALSE(HasPrefixDir("c:/home/foobar", "c:/home/foo"));
#endif
}

TEST(PathUtilTest, GetFileNameExtension) {
  EXPECT_EQ("txt", GetFileNameExtension("a.txt"));
  EXPECT_EQ("",    GetFileNameExtension("a."));
  EXPECT_EQ("",    GetFileNameExtension(""));
  EXPECT_EQ("",    GetFileNameExtension("/"));
  EXPECT_EQ("",    GetFileNameExtension("a"));
  EXPECT_EQ("",    GetFileNameExtension("a/"));
  EXPECT_EQ("txt", GetFileNameExtension("/a/b/c.txt"));
  EXPECT_EQ("cc",  GetFileNameExtension("/a/b.c/d/e.cc"));
  EXPECT_EQ("",    GetFileNameExtension("/a/b.c/d/e"));
  EXPECT_EQ("g",   GetFileNameExtension("/a/b.c/d/e.f.g"));

  EXPECT_EQ("",    GetFileNameExtension("a:\\"));
  EXPECT_EQ("",    GetFileNameExtension("a:\\b\\"));
  EXPECT_EQ("txt", GetFileNameExtension("a:\\b\\c.txt"));
  EXPECT_EQ("cc",  GetFileNameExtension("a:\\b.c\\d\\e.cc"));
  EXPECT_EQ("",    GetFileNameExtension("a:\\b.c\\d\\e"));
  EXPECT_EQ("g",   GetFileNameExtension("a:\\b.c\\d\\e.f.g"));

  EXPECT_EQ("",    GetFileNameExtension("a:/"));
  EXPECT_EQ("",    GetFileNameExtension("a:/b/"));
  EXPECT_EQ("txt", GetFileNameExtension("a:/b/c.txt"));
  EXPECT_EQ("cc",  GetFileNameExtension("a:/b.c/d/e.cc"));
  EXPECT_EQ("",    GetFileNameExtension("a:/b.c/d/e"));
  EXPECT_EQ("g",   GetFileNameExtension("a:/b.c/d/e.f.g"));

  EXPECT_EQ("",   GetFileNameExtension(".cshrc"));
  EXPECT_EQ("",   GetFileNameExtension("/home/user/.cshrc"));
  EXPECT_EQ("",   GetFileNameExtension("c:\\.netrc"));
}

}  // namespace devtools_goma
