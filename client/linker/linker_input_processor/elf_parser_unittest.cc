// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "elf_parser.h"
#include "file_dir.h"
#include "mypath.h"
#include "path.h"
#include "simple_timer.h"
#include "subprocess.h"

using std::string;

namespace devtools_goma {

class ElfParserTest : public testing::Test {
 protected:
  void SetUp() override {
    data_dir_ = file::JoinPath(GetMyDirectory(), "../../test");
  }

  void GetObjdumpOutput(const string& filename, std::vector<string>* needed) {
    std::vector<string> argv;
    argv.push_back("objdump");
    argv.push_back("-p");
    argv.push_back(filename);
    std::vector<string> env;
    env.push_back("LC_ALL=C");
    string output = ReadCommandOutputByPopen("objdump", argv, env, ".",
                                             MERGE_STDOUT_STDERR, nullptr);
    size_t pos = 0;
    while ((pos = output.find("NEEDED", pos)) != string::npos) {
      pos += strlen("NEEDED");
      while (pos < output.size()) {
        if (output[pos] != ' ')
          break;
        ++pos;
      }
      size_t spos = pos;
      while (pos < output.size()) {
        if (output[pos] == '\n')
          break;
        ++pos;
      }
      needed->push_back(output.substr(spos, pos - spos));
      ++pos;
      if (output[pos] != ' ')
        break;
      ++pos;
    }
  }

  string data_dir_;
};

TEST_F(ElfParserTest, GetObjdumpOutput) {
  std::vector<string> needed;
  GetObjdumpOutput(file::JoinPath(data_dir_, "libdl.so"), &needed);
  EXPECT_EQ(2U, needed.size());
  EXPECT_EQ("libc.so.6", needed[0]);
  EXPECT_EQ("ld-linux-x86-64.so.2", needed[1]);
}

TEST_F(ElfParserTest, ReadDynamicNeeded) {
  std::unique_ptr<ElfParser> parser(ElfParser::NewElfParser(
      file::JoinPath(data_dir_, "libdl.so")));
  ASSERT_TRUE(parser != nullptr);
  EXPECT_TRUE(parser->valid());
  std::vector<string> needed;
  EXPECT_TRUE(parser->ReadDynamicNeeded(&needed));
  EXPECT_EQ(2U, needed.size());
  EXPECT_EQ("libc.so.6", needed[0]);
  EXPECT_EQ("ld-linux-x86-64.so.2", needed[1]);
}

TEST_F(ElfParserTest, IsElf) {
  EXPECT_TRUE(ElfParser::IsElf(file::JoinPath(data_dir_, "libdl.so")));
  EXPECT_FALSE(ElfParser::IsElf(file::JoinPath(data_dir_, "libc.so")));
}

TEST_F(ElfParserTest, UsrLib) {
  std::vector<DirEntry> entries;
  ASSERT_TRUE(ListDirectory("/usr/lib", &entries));
  int num = 0;
  SimpleTimer timer;
  absl::Duration elf_parser_p_time;
  absl::Duration elf_parser_s_time;
  absl::Duration objdump_time;
  for (size_t i = 0; i < entries.size(); ++i) {
    string name = entries[i].name;
    if (name == "." || name == "..")
      continue;
    string fullname = file::JoinPath("/usr/lib", name);
    VLOG(1) << fullname;
    if (fullname.find(".so") == string::npos)
      continue;
    struct stat st;
    if (stat(fullname.c_str(), &st) < 0)
      continue;
    if (!S_ISREG(st.st_mode))
      continue;
    if (!ElfParser::IsElf(fullname))
      continue;

    std::vector<string> p_needed;
    timer.Start();
    std::unique_ptr<ElfParser> parser(ElfParser::NewElfParser(fullname));
    ASSERT_TRUE(parser != nullptr) << fullname;
    EXPECT_TRUE(parser->valid()) << fullname;
    parser->UseProgramHeader(true);
    EXPECT_TRUE(parser->ReadDynamicNeeded(&p_needed)) << fullname;
    elf_parser_p_time += timer.GetDuration();

    std::vector<string> s_needed;
    timer.Start();
    parser = ElfParser::NewElfParser(fullname);
    ASSERT_TRUE(parser != nullptr) << fullname;
    EXPECT_TRUE(parser->valid()) << fullname;
    parser->UseProgramHeader(false);
    EXPECT_TRUE(parser->ReadDynamicNeeded(&s_needed)) << fullname;
    elf_parser_s_time += timer.GetDuration();

    std::vector<string> expected_needed;
    timer.Start();
    GetObjdumpOutput(fullname, &expected_needed);
    objdump_time += timer.GetDuration();

    EXPECT_EQ(expected_needed, p_needed) << fullname;
    EXPECT_EQ(expected_needed, s_needed) << fullname;
    ++num;
  }
  EXPECT_GT(num, 0);
  LOG(INFO) << "check elf files:" << num;
  LOG(INFO) << "time"
            << " p:" << elf_parser_p_time
            << " s:" << elf_parser_s_time
            << " objdump:" << objdump_time;
}

TEST_F(ElfParserTest, ReadDynamicNeededAndRpath) {
  std::vector<string> argv;
  std::vector<string> env;
  argv.push_back("gcc");
  argv.push_back("-xc");
  argv.push_back("/dev/null");
  argv.push_back("-shared");
  argv.push_back("-Wl,-rpath=/lib");
  argv.push_back("-o");
  argv.push_back("/tmp/null.so");
  ReadCommandOutputByPopen("gcc", argv, env, ".", MERGE_STDOUT_STDERR, nullptr);

  std::unique_ptr<ElfParser> parser(ElfParser::NewElfParser("/tmp/null.so"));
  ASSERT_TRUE(parser != nullptr);
  EXPECT_TRUE(parser->valid());
  std::vector<string> needed, rpath;
  EXPECT_TRUE(parser->ReadDynamicNeededAndRpath(&needed, &rpath));
  EXPECT_EQ(1U, needed.size());
  EXPECT_EQ("libc.so.6", needed[0]);
  EXPECT_EQ(1U, rpath.size());
  EXPECT_EQ("/lib", rpath[0]);
}

}  // namespace devtools_goma
