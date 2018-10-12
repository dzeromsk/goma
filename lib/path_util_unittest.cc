// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/path_util.h"

#include "gtest/gtest.h"

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

TEST(PathUtilTest, GetDirname) {
  EXPECT_EQ("/a",    GetDirname("/a/"));
  EXPECT_EQ("/",     GetDirname("/a"));
  EXPECT_EQ("a",     GetDirname("a/b"));
  EXPECT_EQ("a",     GetDirname("a/"));
  EXPECT_EQ("",      GetDirname("a"));
  EXPECT_EQ("",      GetDirname("ab"));
  EXPECT_EQ("/",     GetDirname("/"));
  EXPECT_EQ("",      GetDirname(""));
  EXPECT_EQ("/a/b",  GetDirname("/a/b/c.txt"));

  EXPECT_EQ("\\a",   GetDirname("\\a\\"));
  EXPECT_EQ("\\",    GetDirname("\\a"));
  EXPECT_EQ("a",     GetDirname("a\\b"));
  EXPECT_EQ("a",     GetDirname("a\\"));
  EXPECT_EQ("\\",    GetDirname("\\"));

  EXPECT_EQ("a:\\",  GetDirname("a:\\"));
  EXPECT_EQ("a:\\b", GetDirname("a:\\b\\"));
  EXPECT_EQ("a:\\b", GetDirname("a:\\b\\c.txt"));
  EXPECT_EQ("a:/",   GetDirname("a:/"));
  EXPECT_EQ("a:/b",  GetDirname("a:/b/"));
  EXPECT_EQ("a:/b",  GetDirname("a:/b/c.txt"));

  EXPECT_EQ("a:b",   GetDirname("a:b\\c"));
  EXPECT_EQ("a:",    GetDirname("a:b"));
}

TEST(PathUtilTest, GetBasename) {
  EXPECT_EQ("",       GetBasename("/a/"));
  EXPECT_EQ("a",      GetBasename("/a"));
  EXPECT_EQ("b",      GetBasename("a/b"));
  EXPECT_EQ("",       GetBasename("a/"));
  EXPECT_EQ("a",      GetBasename("a"));
  EXPECT_EQ("",       GetBasename("/"));
  EXPECT_EQ("",       GetBasename(""));
  EXPECT_EQ("c.txt",  GetBasename("/a/b/c.txt"));

  EXPECT_EQ("",       GetBasename("a:\\"));
  EXPECT_EQ("",       GetBasename("a:\\b\\"));
  EXPECT_EQ("c.txt",  GetBasename("a:\\b\\c.txt"));

  EXPECT_EQ("",       GetBasename("a:/"));
  EXPECT_EQ("",       GetBasename("a:/b/"));
  EXPECT_EQ("c.txt",  GetBasename("a:/b/c.txt"));

  EXPECT_EQ(".cshrc", GetBasename(".cshrc"));
  EXPECT_EQ(".cshrc", GetBasename("/home/user/.cshrc"));
  EXPECT_EQ(".netrc", GetBasename("c:\\.netrc"));
}

TEST(PathUtilTest, GetExtension) {
  EXPECT_EQ("txt", GetExtension("a.txt"));
  EXPECT_EQ("",    GetExtension("a."));
  EXPECT_EQ("",    GetExtension(""));
  EXPECT_EQ("",    GetExtension("/"));
  EXPECT_EQ("",    GetExtension("a"));
  EXPECT_EQ("",    GetExtension("a/"));
  EXPECT_EQ("txt", GetExtension("/a/b/c.txt"));
  EXPECT_EQ("cc",  GetExtension("/a/b.c/d/e.cc"));
  EXPECT_EQ("",    GetExtension("/a/b.c/d/e"));
  EXPECT_EQ("g",   GetExtension("/a/b.c/d/e.f.g"));

  EXPECT_EQ("",    GetExtension("a:\\"));
  EXPECT_EQ("",    GetExtension("a:\\b\\"));
  EXPECT_EQ("txt", GetExtension("a:\\b\\c.txt"));
  EXPECT_EQ("cc",  GetExtension("a:\\b.c\\d\\e.cc"));
  EXPECT_EQ("",    GetExtension("a:\\b.c\\d\\e"));
  EXPECT_EQ("g",   GetExtension("a:\\b.c\\d\\e.f.g"));

  EXPECT_EQ("",    GetExtension("a:/"));
  EXPECT_EQ("",    GetExtension("a:/b/"));
  EXPECT_EQ("txt", GetExtension("a:/b/c.txt"));
  EXPECT_EQ("cc",  GetExtension("a:/b.c/d/e.cc"));
  EXPECT_EQ("",    GetExtension("a:/b.c/d/e"));
  EXPECT_EQ("g",   GetExtension("a:/b.c/d/e.f.g"));

  EXPECT_EQ("",    GetExtension(".cshrc"));
  EXPECT_EQ("",    GetExtension("/home/user/.cshrc"));
  EXPECT_EQ("",    GetExtension("c:\\.netrc"));
}

TEST(PathUtilTest, GetStem) {
  EXPECT_EQ("a",      GetStem("a.txt"));
  EXPECT_EQ("a",      GetStem("a."));
  EXPECT_EQ("",       GetStem(""));
  EXPECT_EQ("",       GetStem("/"));
  EXPECT_EQ("a",      GetStem("a"));
  EXPECT_EQ("",       GetStem("a/"));
  EXPECT_EQ("c",      GetStem("/a/b/c.txt"));
  EXPECT_EQ("e",      GetStem("/a/b.c/d/e.cc"));
  EXPECT_EQ("e",      GetStem("/a/b.c/d/e"));
  EXPECT_EQ("e.f",    GetStem("/a/b.c/d/e.f.g"));

  EXPECT_EQ("",       GetStem("a:\\"));
  EXPECT_EQ("",       GetStem("a:\\b\\"));
  EXPECT_EQ("c",      GetStem("a:\\b\\c.txt"));
  EXPECT_EQ("e",      GetStem("a:\\b.c\\d\\e.cc"));
  EXPECT_EQ("e",      GetStem("a:\\b.c\\d\\e"));
  EXPECT_EQ("e.f",    GetStem("a:\\b.c\\d\\e.f.g"));

  EXPECT_EQ("",       GetStem("a:/"));
  EXPECT_EQ("",       GetStem("a:/b/"));
  EXPECT_EQ("c",      GetStem("a:/b/c.txt"));
  EXPECT_EQ("e",      GetStem("a:/b.c/d/e.cc"));
  EXPECT_EQ("e",      GetStem("a:/b.c/d/e"));
  EXPECT_EQ("e.f",    GetStem("a:/b.c/d/e.f.g"));

  EXPECT_EQ(".cshrc", GetStem(".cshrc"));
  EXPECT_EQ(".cshrc", GetStem("/home/user/.cshrc"));
  EXPECT_EQ(".netrc", GetStem("c:\\.netrc"));
}

}  // namespace devtools_goma
