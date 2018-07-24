// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <limits.h>
#include <stdio.h>

#include <glog/logging.h>
#include <glog/stl_logging.h>

#ifndef _WIN32
#include <unistd.h>
#else
# include "absl/strings/string_view.h"
# include "config_win.h"
#endif
#ifdef __MACH__
#include <ar.h>
#include <mach-o/ranlib.h>
#endif

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "absl/memory/memory.h"
#include "arfile.h"
#include "mypath.h"
#include "unittest_util.h"
#include "util.h"

// Note: There's no ar/cc in Windows.  As a result, the commands used in the
//       test cases are run on a Linux machine and the data files are carried
//       over in build/testdata

namespace devtools_goma {

class ArFileTest : public testing::Test {
  void SetUp() override {
    cwd_ = GetCurrentDirNameOrDie();
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("arfile_unittest");
    PCHECK(Chdir(tmpdir_util_->tmpdir().c_str()));
  }

  void TearDown() override {
    PCHECK(Chdir(cwd_.c_str()));
    tmpdir_util_.reset();
  }

 protected:
  void Compile(const std::string& output) {
#ifndef _WIN32
    std::stringstream ss;
    ss << "echo 'int x;' | cc -xc -o " << output << " -c -";
    PCHECK(system(ss.str().c_str()) == 0);
#else
    UNREFERENCED_PARAMETER(output);
#endif
  }

#ifndef _WIN32
  void Archive(const std::string& op, const std::string& archive,
               const std::vector<std::string>& files) {
#else
  void Archive(const std::string& test_name, const std::string& archive) {
#endif
#ifndef _WIN32
    std::stringstream ss;
    ss << "ar " << op << " " << archive;
    for (size_t i = 0; i < files.size(); ++i) {
      ss << " " << files[i];
    }
    PCHECK(system(ss.str().c_str()) == 0);
#else
    CopyFileA(GetTestFilePath(test_name + ".a").c_str(),
                              archive.c_str(), FALSE);
#endif
  }

 protected:
  std::string cwd_;
  std::unique_ptr<TmpdirUtil> tmpdir_util_;
};

TEST_F(ArFileTest, NotThinArchive) {
  std::vector<std::string> files;
  files.push_back("x.o");
#ifndef _WIN32
  Compile("x.o");
  Archive("rcu", "t.a", files);
#else
  Archive("NotThinArchive", "t.a");
#endif
  ArFile a("t.a");
  EXPECT_TRUE(a.Exists());
  EXPECT_FALSE(a.IsThinArchive());
  std::vector<ArFile::EntryHeader> entries;
  a.GetEntries(&entries);
  CHECK_EQ(1U, files.size());
  EXPECT_EQ(files.size(), entries.size());
  EXPECT_EQ(files[0], entries[0].ar_name);
}

#ifndef __MACH__    // We usually do not use thin archive and long name on mac.
TEST_F(ArFileTest, ThinArchive) {
  std::vector<std::string> files;
  files.push_back("x.o");
#ifndef _WIN32
  Compile("x.o");
  Archive("rcuT", "t.a", files);
#else
  Archive("ThinArchive", "t.a");
#endif
  ArFile a("t.a");
  EXPECT_TRUE(a.Exists());
  EXPECT_TRUE(a.IsThinArchive());
  std::vector<ArFile::EntryHeader> entries;
  a.GetEntries(&entries);
  CHECK_EQ(1U, files.size());
  EXPECT_EQ(files.size(), entries.size());
  EXPECT_EQ(files[0], entries[0].ar_name);
}

TEST_F(ArFileTest, NotThinArchiveLongName) {
  std::vector<std::string> files;
  files.push_back("long_long_long_long_name.o");
  files.push_back("long_long_long_long_name1.o");
  files.push_back("long_long_long_long_name2.o");
  files.push_back("long_long_long_long_name3.o");
#ifndef _WIN32
  Compile("long_long_long_long_name.o");
  Compile("long_long_long_long_name1.o");
  Compile("long_long_long_long_name2.o");
  Compile("long_long_long_long_name3.o");
  Archive("rcu", "t.a", files);
#else
  Archive("NotThinArchiveLongName", "t.a");
#endif
  ArFile a("t.a");
  EXPECT_TRUE(a.Exists());
  EXPECT_FALSE(a.IsThinArchive());
  std::vector<ArFile::EntryHeader> entries;
  a.GetEntries(&entries);
  CHECK_EQ(4U, files.size());
  EXPECT_EQ(files.size(), entries.size());
  EXPECT_EQ(files[0], entries[0].ar_name);
  EXPECT_EQ(files[1], entries[1].ar_name);
  EXPECT_EQ(files[2], entries[2].ar_name);
  EXPECT_EQ(files[3], entries[3].ar_name);
}

TEST_F(ArFileTest, ThinArchiveLongName) {
  std::vector<std::string> files;
  files.push_back("long_long_long_long_name.o");
  files.push_back("long_long_long_long_name1.o");
  files.push_back("long_long_long_long_name2.o");
  files.push_back("long_long_long_long_name3.o");
#ifndef _WIN32
  Compile("long_long_long_long_name.o");
  Compile("long_long_long_long_name1.o");
  Compile("long_long_long_long_name2.o");
  Compile("long_long_long_long_name3.o");
  Archive("rcuT", "t.a", files);
#else
  Archive("ThinArchiveLongName", "t.a");
#endif
  ArFile a("t.a");
  EXPECT_TRUE(a.Exists());
  EXPECT_TRUE(a.IsThinArchive());
  std::vector<ArFile::EntryHeader> entries;
  a.GetEntries(&entries);
  CHECK_EQ(4U, files.size());
  EXPECT_EQ(files.size(), entries.size());
  EXPECT_EQ(files[0], entries[0].ar_name);
  EXPECT_EQ(files[1], entries[1].ar_name);
  EXPECT_EQ(files[2], entries[2].ar_name);
  EXPECT_EQ(files[3], entries[3].ar_name);
}
#endif  // __MACH__

TEST_F(ArFileTest, ArEntryHeaderSize) {
  ArFile::EntryHeader entry_header;
  std::string buf;

  EXPECT_TRUE(entry_header.SerializeToString(&buf));
  // according to the spec, sizeof(struct ar_hdr) == 60.
  EXPECT_EQ(60U, buf.length());
}

TEST_F(ArFileTest, ArEntryHeader) {
  ArFile::EntryHeader entry_header;
  std::string buf;
  entry_header.orig_ar_name = "test";
  entry_header.orig_ar_name.append(16 - 4, ' ');
  entry_header.ar_date = 12;
  entry_header.ar_uid = 34;
  entry_header.ar_gid = 56;
  entry_header.ar_mode = 07;
  entry_header.ar_size = 89;
  std::string expected;
  // ar_name (16 bytes, decimal)
  expected.append("test");
  expected.append(16 - 4, ' ');
  // ar_date (12 bytes, decimal)
  expected.append("12");
  expected.append(12 - 2, ' ');
  // ar_uid (6 bytes, decimal)
  expected.append("34");
  expected.append(6 - 2, ' ');
  // ar_gid (6 bytes, decimal)
  expected.append("56");
  expected.append(6 - 2, ' ');
  // ar_mode (8 bytes, octal)
  expected.append("7");
  expected.append(8 - 1, ' ');
  // ar_size (10 bytes, decimal)
  expected.append("89");
  expected.append(10 - 2, ' ');
  // ar_fmag (2 bytes, magic)
  expected.append("`\n");

  EXPECT_TRUE(entry_header.SerializeToString(&buf));
  // according to the spec, sizeof(struct ar_hdr) == 60.
  EXPECT_EQ(expected, buf);
}

TEST_F(ArFileTest, CleanIfRanlibTest) {
#ifdef __MACH__
  // How MacDirtyRanlib.a is created:
  // % echo 'void test(){}' | cc -xc -o test.o -c -
  // % ar rcu MacDirtyRanlib.a test.o
  // % bvi MacDirtyRanlib.a
  //   (Add garbage in string area)
  ArFile a(GetTestFilePath("MacDirtyRanlib.a"));
  EXPECT_TRUE(a.Exists());
  EXPECT_FALSE(a.IsThinArchive());

  // Skip header.
  std::string header;
  EXPECT_TRUE(a.ReadHeader(&header));

  // Read entry.
  ArFile::EntryHeader entry_header;
  std::string entry_body;
  EXPECT_TRUE(a.ReadEntry(&entry_header, &entry_body));

  // Pick up string area.
  //
  // Format of the ranlib entry:
  // ar header
  // SYMDEF magic (e.g. __.SYMDEF SORTED): 20 bytes
  // ranlib area size: 4 bytes.
  // ranlib area
  // string area size: 4 bytes.
  // string area.
  const size_t string_pos = 20 + 4 + sizeof(ranlib) + 4;
  std::string actual = entry_body.substr(string_pos);
  std::string expected = actual;
  const size_t len = strlen(&actual[0]);

  EXPECT_GT(expected.size() - len, 1U);
  memset(&expected[len], '\0', expected.size() - len);
  EXPECT_EQ(expected, actual);

  // Making doubly sure.
  // Fill the end of string area with garbage.
  for (size_t i = 0; i < expected.size() - len - 1; ++i)
    entry_body[entry_body.size() - 1 - i] = '\xff';
  actual = entry_body.substr(string_pos, expected.size());
  EXPECT_NE(expected, actual);
  EXPECT_TRUE(ArFile::CleanIfRanlib(entry_header, &entry_body));
  actual = entry_body.substr(string_pos, expected.size());
  EXPECT_EQ(expected, actual);
#endif
}

}  // namespace devtools_goma
