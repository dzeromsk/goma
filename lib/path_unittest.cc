// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "path.h"

#include "gtest/gtest.h"
using std::string;

TEST(PathTest, fileJoinPath) {
  EXPECT_EQ("", file::JoinPath());
  EXPECT_EQ("", file::JoinPath(""));
  EXPECT_EQ("", file::JoinPath("", ""));
  EXPECT_EQ("", file::JoinPath("", "", ""));

  EXPECT_EQ("a", file::JoinPath("a"));
  EXPECT_EQ("/a", file::JoinPath("/a"));
  EXPECT_EQ("a/", file::JoinPath("a/"));
  EXPECT_EQ("/a/", file::JoinPath("/a/"));

  EXPECT_EQ("a", file::JoinPath("a", ""));
  EXPECT_EQ("/a", file::JoinPath("/a", ""));
  EXPECT_EQ("a/", file::JoinPath("a/", ""));
  EXPECT_EQ("/a/", file::JoinPath("/a/", ""));

  EXPECT_EQ("a", file::JoinPath("", "a"));
  EXPECT_EQ("/a", file::JoinPath("", "/a"));
  EXPECT_EQ("a/", file::JoinPath("", "a/"));
  EXPECT_EQ("/a/", file::JoinPath("", "/a/"));

  EXPECT_EQ("a", file::JoinPath("a", "", ""));
  EXPECT_EQ("a", file::JoinPath("", "a", ""));
  EXPECT_EQ("a", file::JoinPath("", "", "a"));

#ifndef _WIN32
  EXPECT_EQ("a/b", file::JoinPath("a", "b"));
  EXPECT_EQ("a/b/", file::JoinPath("a", "b/"));
  EXPECT_EQ("a/b", file::JoinPath("a", "/b"));
  EXPECT_EQ("a/b/", file::JoinPath("a", "/b/"));

  EXPECT_EQ("a/b", file::JoinPath("a/", "b"));
  EXPECT_EQ("a/b/", file::JoinPath("a/", "b/"));
  EXPECT_EQ("a/b", file::JoinPath("a/", "/b"));
  EXPECT_EQ("a/b/", file::JoinPath("a/", "/b/"));

  EXPECT_EQ("/a/b", file::JoinPath("/a", "b"));
  EXPECT_EQ("/a/b/", file::JoinPath("/a", "b/"));
  EXPECT_EQ("/a/b", file::JoinPath("/a", "/b"));
  EXPECT_EQ("/a/b/", file::JoinPath("/a", "/b/"));

  EXPECT_EQ("/a/b", file::JoinPath("/a/", "b"));
  EXPECT_EQ("/a/b/", file::JoinPath("/a/", "b/"));
  EXPECT_EQ("/a/b", file::JoinPath("/a/", "/b"));
  EXPECT_EQ("/a/b/", file::JoinPath("/a/", "/b/"));

  EXPECT_EQ("a/a", file::JoinPath("a", "a", ""));
  EXPECT_EQ("a/a", file::JoinPath("", "a", "a"));
  EXPECT_EQ("a/a", file::JoinPath("a", "", "a"));

  EXPECT_EQ("a/b/c/d/e", file::JoinPath("a", "b", "c", "d", "e"));
  EXPECT_EQ("/a/b/c/d/e", file::JoinPath("/a", "/b", "/c", "/d", "/e"));
  EXPECT_EQ("a/b/c/d/e/", file::JoinPath("a/", "b/", "c/", "d/", "e/"));
  EXPECT_EQ("/a/b/c/d/e/", file::JoinPath("/a/", "/b/", "/c/", "/d/", "/e/"));
#else
  EXPECT_EQ("\\a", file::JoinPath("\\a"));
  EXPECT_EQ("a\\", file::JoinPath("a\\"));
  EXPECT_EQ("\\a\\", file::JoinPath("\\a\\"));

  EXPECT_EQ("a\\b", file::JoinPath("a", "b"));
  EXPECT_EQ("a\\b\\", file::JoinPath("a", "b\\"));
  EXPECT_EQ("a\\b", file::JoinPath("a", "\\b"));
  EXPECT_EQ("a\\b\\", file::JoinPath("a", "\\b\\"));

  EXPECT_EQ("a\\b", file::JoinPath("a\\", "b"));
  EXPECT_EQ("a\\b\\", file::JoinPath("a\\", "b\\"));
  EXPECT_EQ("a\\b", file::JoinPath("a\\", "\\b"));
  EXPECT_EQ("a\\b\\", file::JoinPath("a\\", "\\b\\"));

  EXPECT_EQ("\\a\\b", file::JoinPath("\\a", "b"));
  EXPECT_EQ("\\a\\b\\", file::JoinPath("\\a", "b\\"));
  EXPECT_EQ("\\a\\b", file::JoinPath("\\a", "\\b"));
  EXPECT_EQ("\\a\\b\\", file::JoinPath("\\a", "\\b\\"));

  EXPECT_EQ("\\a\\b", file::JoinPath("\\a\\", "b"));
  EXPECT_EQ("\\a\\b\\", file::JoinPath("\\a\\", "b\\"));
  EXPECT_EQ("\\a\\b", file::JoinPath("\\a\\", "\\b"));
  EXPECT_EQ("\\a\\b\\", file::JoinPath("\\a\\", "\\b\\"));

  EXPECT_EQ("c:\\b", file::JoinPath("", "c:\\b"));
  EXPECT_EQ("a\\c:\\b", file::JoinPath("a", "c:\\b"));
  EXPECT_EQ("\\a\\c:\\b", file::JoinPath("\\a", "c:\\b"));
  EXPECT_EQ("\\a\\c:\\b", file::JoinPath("\\a\\", "c:\\b"));
  EXPECT_EQ("a\\c:\\b", file::JoinPath("a\\", "c:\\b"));

  EXPECT_EQ("c:\\a\\b", file::JoinPath("c:\\a", "b"));
  EXPECT_EQ("c:\\a\\b\\", file::JoinPath("c:\\a", "b\\"));
  EXPECT_EQ("c:\\a\\b", file::JoinPath("c:\\a", "\\b"));
  EXPECT_EQ("c:\\a\\b\\", file::JoinPath("c:\\a", "\\b\\"));
  EXPECT_EQ("c:\\a\\b", file::JoinPath("c:\\a\\", "b"));
  EXPECT_EQ("c:\\a\\b\\", file::JoinPath("c:\\a\\", "b\\"));
  EXPECT_EQ("c:\\a\\b", file::JoinPath("c:\\a\\", "\\b"));
  EXPECT_EQ("c:\\a\\b\\", file::JoinPath("c:\\a\\", "\\b\\"));

  EXPECT_EQ("a\\a", file::JoinPath("a", "a", ""));
  EXPECT_EQ("a\\a", file::JoinPath("", "a", "a"));
  EXPECT_EQ("a\\a", file::JoinPath("a", "", "a"));

  EXPECT_EQ("a\\b\\c\\d\\e",
            file::JoinPath("a", "b", "c", "d", "e"));
  EXPECT_EQ("\\a\\b\\c\\d\\e",
            file::JoinPath("\\a", "\\b", "\\c", "\\d", "\\e"));
  EXPECT_EQ("a\\b\\c\\d\\e\\",
            file::JoinPath("a\\", "b\\", "c\\", "d\\", "e\\"));
  EXPECT_EQ("\\a\\b\\c\\d\\e\\",
            file::JoinPath("\\a\\", "\\b\\", "\\c\\", "\\d\\", "\\e\\"));

  // Unix style should also work.
  EXPECT_EQ("/a\\b", file::JoinPath("/a", "b"));
  EXPECT_EQ("/a\\b", file::JoinPath("/a", "/b"));
  EXPECT_EQ("/a/b", file::JoinPath("/a/", "b"));
  EXPECT_EQ("/a/b", file::JoinPath("/a/", "/b"));
  EXPECT_EQ("a\\b", file::JoinPath("a", "/b"));

  EXPECT_EQ("/a\\b\\c\\d\\e",
            file::JoinPath("/a", "/b", "/c", "/d", "/e"));
  EXPECT_EQ("a/b/c/d/e/",
            file::JoinPath("a/", "b/", "c/", "d/", "e/"));
  EXPECT_EQ("/a/b/c/d/e/",
            file::JoinPath("/a/", "/b/", "/c/", "/d/", "/e/"));
#endif
}

TEST(PathTest, fileJoinPathRespectAbsolute) {
  EXPECT_EQ("", file::JoinPathRespectAbsolute());
  EXPECT_EQ("", file::JoinPathRespectAbsolute(""));
  EXPECT_EQ("", file::JoinPathRespectAbsolute("", ""));
  EXPECT_EQ("", file::JoinPathRespectAbsolute("", "", ""));

  EXPECT_EQ("a", file::JoinPathRespectAbsolute("a"));
  EXPECT_EQ("/a", file::JoinPathRespectAbsolute("/a"));
  EXPECT_EQ("a/", file::JoinPathRespectAbsolute("a/"));
  EXPECT_EQ("/a/", file::JoinPathRespectAbsolute("/a/"));

  EXPECT_EQ("a", file::JoinPathRespectAbsolute("a", ""));
  EXPECT_EQ("/a", file::JoinPathRespectAbsolute("/a", ""));
  EXPECT_EQ("a/", file::JoinPathRespectAbsolute("a/", ""));
  EXPECT_EQ("/a/", file::JoinPathRespectAbsolute("/a/", ""));

  EXPECT_EQ("a", file::JoinPathRespectAbsolute("", "a"));
  EXPECT_EQ("/a", file::JoinPathRespectAbsolute("", "/a"));
  EXPECT_EQ("a/", file::JoinPathRespectAbsolute("", "a/"));
  EXPECT_EQ("/a/", file::JoinPathRespectAbsolute("", "/a/"));

  EXPECT_EQ("a", file::JoinPathRespectAbsolute("a", "", ""));
  EXPECT_EQ("a", file::JoinPathRespectAbsolute("", "a", ""));
  EXPECT_EQ("a", file::JoinPathRespectAbsolute("", "", "a"));

#ifndef _WIN32
  EXPECT_EQ("a/b", file::JoinPathRespectAbsolute("a", "b"));
  EXPECT_EQ("/b", file::JoinPathRespectAbsolute("a", "/b"));
  EXPECT_EQ("/c", file::JoinPathRespectAbsolute("a", "/b", "/c"));
  EXPECT_EQ("/b/c", file::JoinPathRespectAbsolute("a", "/b", "c"));

  EXPECT_EQ("/a/b", file::JoinPathRespectAbsolute("/a", "b"));
  EXPECT_EQ("/b", file::JoinPathRespectAbsolute("/a", "/b"));
  EXPECT_EQ("/c", file::JoinPathRespectAbsolute("/a", "/b", "/c"));
  EXPECT_EQ("/b/c", file::JoinPathRespectAbsolute("/a", "/b", "c"));

  EXPECT_EQ("/a/b", file::JoinPathRespectAbsolute("/a/", "b"));
  EXPECT_EQ("/b", file::JoinPathRespectAbsolute("/a/", "/b"));
  EXPECT_EQ("/c", file::JoinPathRespectAbsolute("/a/", "/b", "/c"));
  EXPECT_EQ("/b/c", file::JoinPathRespectAbsolute("/a/", "/b", "c"));
#else
  EXPECT_EQ("a\\b", file::JoinPathRespectAbsolute("a", "b"));
  EXPECT_EQ("c:\\b", file::JoinPathRespectAbsolute("a", "c:\\b"));
  EXPECT_EQ("c:\\c", file::JoinPathRespectAbsolute("a", "c:\\b", "c:\\c"));
  EXPECT_EQ("c:\\b\\c", file::JoinPathRespectAbsolute("a", "c:\\b", "c"));

  EXPECT_EQ("\\a\\b", file::JoinPathRespectAbsolute("\\a", "b"));
  EXPECT_EQ("c:\\b", file::JoinPathRespectAbsolute("\\a", "c:\\b"));
  EXPECT_EQ("c:\\c", file::JoinPathRespectAbsolute("\\a", "c:\\b", "c:\\c"));
  EXPECT_EQ("c:\\b\\c", file::JoinPathRespectAbsolute("\\a", "c:\\b", "c"));

  EXPECT_EQ("\\a\\b", file::JoinPathRespectAbsolute("\\a\\", "b"));
  EXPECT_EQ("c:\\b", file::JoinPathRespectAbsolute("\\a\\", "c:\\b"));
  EXPECT_EQ("c:\\c", file::JoinPathRespectAbsolute("\\a\\", "c:\\b", "c:\\c"));
  EXPECT_EQ("c:\\b\\c", file::JoinPathRespectAbsolute("\\a\\", "c:\\b", "c"));

  EXPECT_EQ("c:\\a\\b", file::JoinPathRespectAbsolute("c:\\a", "b"));
  EXPECT_EQ("c:\\b", file::JoinPathRespectAbsolute("c:\\a", "c:\\b"));
  EXPECT_EQ("c:\\c", file::JoinPathRespectAbsolute("c:\\a", "c:\\b", "c:\\c"));
  EXPECT_EQ("c:\\b\\c", file::JoinPathRespectAbsolute("c:\\a", "c:\\b", "c"));

  EXPECT_EQ("c:\\a\\b", file::JoinPathRespectAbsolute("c:\\a\\", "b"));
  EXPECT_EQ("c:\\b", file::JoinPathRespectAbsolute("c:\\a\\", "c:\\b"));
  EXPECT_EQ("c:\\c",
            file::JoinPathRespectAbsolute("c:\\a\\", "c:\\b", "c:\\c"));
  EXPECT_EQ("c:\\b\\c", file::JoinPathRespectAbsolute("c:\\a\\", "c:\\b", "c"));

  EXPECT_EQ("\\a\\b", file::JoinPathRespectAbsolute("\\a", "b"));
  EXPECT_EQ("\\b", file::JoinPathRespectAbsolute("\\a", "\\b"));
  EXPECT_EQ("\\a\\b", file::JoinPathRespectAbsolute("\\a\\", "b"));
  EXPECT_EQ("\\b", file::JoinPathRespectAbsolute("\\a\\", "\\b"));

  EXPECT_EQ("\\b", file::JoinPathRespectAbsolute("", "\\b"));
  EXPECT_EQ("c:\\b", file::JoinPathRespectAbsolute("", "c:\\b"));
  EXPECT_EQ("\\a", file::JoinPathRespectAbsolute("\\a", ""));
  EXPECT_EQ("c:\\a", file::JoinPathRespectAbsolute("c:\\a", ""));

  EXPECT_EQ("a\\b", file::JoinPathRespectAbsolute("a", "b"));
  EXPECT_EQ("\\b", file::JoinPathRespectAbsolute("a", "\\b"));
  EXPECT_EQ("c:\\b", file::JoinPathRespectAbsolute("a", "c:\\b"));

  // Unix style should also work.
  EXPECT_EQ("/a\\b", file::JoinPathRespectAbsolute("/a", "b"));
  EXPECT_EQ("/b", file::JoinPathRespectAbsolute("/a", "/b"));
  EXPECT_EQ("/a/b", file::JoinPathRespectAbsolute("/a/", "b"));
  EXPECT_EQ("/b", file::JoinPathRespectAbsolute("/a/", "/b"));
  EXPECT_EQ("/b", file::JoinPathRespectAbsolute("a", "/b"));
#endif
}

TEST(PathTest, fileBasename) {
  EXPECT_EQ("",  file::Basename("/a/"));
  EXPECT_EQ("a", file::Basename("/a"));
  EXPECT_EQ("b", file::Basename("a/b"));
  EXPECT_EQ("",  file::Basename("a/"));
  EXPECT_EQ("",  file::Basename("/"));
  EXPECT_EQ("",  file::Basename(""));

  EXPECT_EQ(".",     file::Basename("."));
  EXPECT_EQ(".a",    file::Basename(".a"));
  EXPECT_EQ("a.",    file::Basename("a."));
  EXPECT_EQ("a.b",   file::Basename("a.b"));
  EXPECT_EQ("a.b.c", file::Basename("a.b.c"));

#ifdef _WIN32
  EXPECT_EQ("",  file::Basename("\\a\\"));
  EXPECT_EQ("a", file::Basename("\\a"));
  EXPECT_EQ("b", file::Basename("a\\b"));
  EXPECT_EQ("",  file::Basename("a\\"));
  EXPECT_EQ("",  file::Basename("\\"));
  // Test with drive letter.
  EXPECT_EQ("",  file::Basename("a:\\"));
  EXPECT_EQ("b", file::Basename("a:\\b"));
  // Test with extension.
  EXPECT_EQ("b.c",   file::Basename("a:\\b.c"));
  EXPECT_EQ("",      file::Basename("a:\\b.c\\"));
  EXPECT_EQ(".",     file::Basename("\\."));
  EXPECT_EQ("",      file::Basename(".\\"));
#endif
}

TEST(PathTest, fileDirname) {
  EXPECT_EQ("/a", file::Dirname("/a/"));
  EXPECT_EQ("/",  file::Dirname("/a"));
  EXPECT_EQ("a",  file::Dirname("a/b"));
  EXPECT_EQ("a",  file::Dirname("a/"));
  EXPECT_EQ("",   file::Dirname("a"));
  EXPECT_EQ("",   file::Dirname("ab"));
  EXPECT_EQ("/",  file::Dirname("/"));
  EXPECT_EQ("",   file::Dirname(""));

#ifdef _WIN32
  EXPECT_EQ("\\a", file::Dirname("\\a\\"));
  EXPECT_EQ("\\",  file::Dirname("\\a"));
  EXPECT_EQ("a",   file::Dirname("a\\b"));
  EXPECT_EQ("a",   file::Dirname("a\\"));
  EXPECT_EQ("\\",  file::Dirname("\\"));
  // Test with drive letter.
  EXPECT_EQ("a:\\", file::Dirname("a:\\"));
  EXPECT_EQ("a:\\", file::Dirname("a:\\b"));
  EXPECT_EQ("a:b",  file::Dirname("a:b\\c"));
  EXPECT_EQ("a:",  file::Dirname("a:b"));
  // Test with extension.
  EXPECT_EQ("a:\\",    file::Dirname("a:\\b.c"));
  EXPECT_EQ("a:\\b.c", file::Dirname("a:\\b.c\\"));
  EXPECT_EQ("\\",      file::Dirname("\\."));
  EXPECT_EQ(".",       file::Dirname(".\\"));
  EXPECT_EQ("a:",  file::Dirname("a:b.txt"));
#endif
}

TEST(PathTest, fileStem) {
  EXPECT_EQ("a",    file::Stem("a.txt"));
  EXPECT_EQ("a",    file::Stem("a."));
  EXPECT_EQ("",     file::Stem(""));
  EXPECT_EQ("",     file::Stem("/"));
  EXPECT_EQ("a",    file::Stem("a"));
  EXPECT_EQ("",     file::Stem("a/"));
  EXPECT_EQ("c",    file::Stem("/a/b/c.c"));
  EXPECT_EQ("e",    file::Stem("/a/b.c/d/e.cc"));
  EXPECT_EQ("e",    file::Stem("/a/b.c/d/e"));
  EXPECT_EQ("e.f",  file::Stem("/a/b.c/d/e.f.g"));

#ifdef _WIN32
  EXPECT_EQ("",     file::Stem("a:\\"));
  EXPECT_EQ("",     file::Stem("a:\\b\\"));
  EXPECT_EQ("c",    file::Stem("a:\\b\\c.c"));
  EXPECT_EQ("e",    file::Stem("a:\\b.c\\d\\e.cc"));
  EXPECT_EQ("e",    file::Stem("a:\\b.c\\d\\e"));
  EXPECT_EQ("e.f",  file::Stem("a:\\b.c\\d\\e.f.g"));
#endif
}

TEST(PathTest, fileExtension) {
  EXPECT_EQ("txt", file::Extension("a.txt"));
  EXPECT_EQ("",    file::Extension("a."));
  EXPECT_EQ("",    file::Extension(""));
  EXPECT_EQ("",    file::Extension("/"));
  EXPECT_EQ("",    file::Extension("a"));
  EXPECT_EQ("",    file::Extension("a/"));
  EXPECT_EQ("txt", file::Extension("/a/b/c.txt"));
  EXPECT_EQ("cc",  file::Extension("/a/b.c/d/e.cc"));
  EXPECT_EQ("",    file::Extension("/a/b.c/d/e"));
  EXPECT_EQ("g",   file::Extension("/a/b.c/d/e.f.g"));

#ifdef _WIN32
  EXPECT_EQ("",    file::Extension("a:\\"));
  EXPECT_EQ("",    file::Extension("a:\\b\\"));
  EXPECT_EQ("txt", file::Extension("a:\\b\\c.txt"));
  EXPECT_EQ("cc",  file::Extension("a:\\b.c\\d\\e.cc"));
  EXPECT_EQ("",    file::Extension("a:\\b.c\\d\\e"));
  EXPECT_EQ("g",   file::Extension("a:\\b.c\\d\\e.f.g"));
#endif
}

TEST(PathTest, fileIsAbsolutePath) {
  // Unix Style.
  EXPECT_FALSE(file::IsAbsolutePath(""));
  EXPECT_FALSE(file::IsAbsolutePath("a"));
  EXPECT_FALSE(file::IsAbsolutePath("../a"));
  EXPECT_FALSE(file::IsAbsolutePath("./a"));
  EXPECT_FALSE(file::IsAbsolutePath("a/b/c/"));
  EXPECT_TRUE(file::IsAbsolutePath("/a"));
  EXPECT_TRUE(file::IsAbsolutePath("/a/b/../c"));

#ifdef _WIN32
  EXPECT_FALSE(file::IsAbsolutePath("..\\a"));
  EXPECT_FALSE(file::IsAbsolutePath("a\\b\\c\\"));
  EXPECT_TRUE(file::IsAbsolutePath("a:"));
  EXPECT_TRUE(file::IsAbsolutePath("a:\\b"));
  EXPECT_TRUE(file::IsAbsolutePath("a:\\b\\..\\c"));
  // Path without drive.
  EXPECT_TRUE(file::IsAbsolutePath("\\a"));
  EXPECT_TRUE(file::IsAbsolutePath("\\a\\b"));
  EXPECT_TRUE(file::IsAbsolutePath("\\a\\b\\..\\c"));
  // UNC path.
  EXPECT_TRUE(file::IsAbsolutePath("\\\\a"));
  EXPECT_TRUE(file::IsAbsolutePath("\\\\a\\b"));
  EXPECT_TRUE(file::IsAbsolutePath("\\\\a\\b\\..\\c"));
#endif
}
