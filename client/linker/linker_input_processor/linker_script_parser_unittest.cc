// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <memory>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "content.h"
#include "gtest/gtest.h"
#include "linker_script_parser.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

class LinkerScriptParserTest : public testing::Test {
 public:
  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("linker_script_test");
    // To be used by LinkerScriptParser::fakeroot_.
    tmpdir_ = tmpdir_util_->tmpdir();
    LinkerScriptParser::fakeroot_ = tmpdir_.c_str();
  }

  void TearDown() override {
    LinkerScriptParser::fakeroot_ = "";
    tmpdir_util_.reset();
  }

 protected:
  std::unique_ptr<TmpdirUtil> tmpdir_util_;
  std::string tmpdir_;
};

#ifndef _WIN32
TEST_F(LinkerScriptParserTest, ParseLibcSo) {
  std::vector<string> searchdirs;
  // Since script do not see inside, I provide empty files.
  tmpdir_util_->CreateTmpFile("/lib/libc.so.6", "");
  tmpdir_util_->CreateTmpFile("/usr/lib/libc_nonshared.a", "");
  tmpdir_util_->CreateTmpFile("/lib/ld-linux-x86-64.so.2", "");
  LinkerScriptParser parser(Content::CreateFromString(
      "/* GNU ld script\n"
      "   Use the shared library, but some functions are only in\n"
      "   the static library, so try that secondarily. */\n"
      "OUTPUT_FORMAT(elf64-x86-64)\n"
      "GROUP ( /lib/libc.so.6 /usr/lib/libc_nonshared.a "
      " AS_NEEDED ( /lib/ld-linux-x86-64.so.2 ) )\n"),
                            "/tmp",
                            searchdirs,
                            "");
  EXPECT_TRUE(parser.Parse());

  EXPECT_EQ("", parser.startup());
  std::vector<string> expected_inputs;
  expected_inputs.push_back("/lib/libc.so.6");
  expected_inputs.push_back("/usr/lib/libc_nonshared.a");
  expected_inputs.push_back("/lib/ld-linux-x86-64.so.2");
  EXPECT_EQ(expected_inputs, parser.inputs());
  EXPECT_EQ("", parser.output());
}
#endif

TEST_F(LinkerScriptParserTest, ParseSample) {
  std::vector<string> searchdirs;
  LinkerScriptParser parser(Content::CreateFromString(
      "SECTIONS\n"
      "{\n"
      "  . = 0x10000;\n"
      "  .text : { *(.text) }\n"
      "  . = 0x8000000;\n"
      "  .data : { *(.data) }\n"
      "  .bss : { *(.bss) }\n"
      "}\n"),
                            "/tmp",
                            searchdirs,
                            "");
  EXPECT_TRUE(parser.Parse());

  EXPECT_EQ("", parser.startup());
  std::vector<string> expected_inputs;
  EXPECT_EQ(expected_inputs, parser.inputs());
  EXPECT_EQ("", parser.output());
}

TEST_F(LinkerScriptParserTest, ParseSample2) {
  std::vector<string> searchdirs;
  LinkerScriptParser parser(Content::CreateFromString(
      "floating_point = 0;\n"
      "SECTIONS\n"
      "{\n"
      "  .text :\n"
      "   {\n"
      "     *(.text)\n"
      "     _etext = .;\n"
      "   }\n"
      "  _bdata = (. + 3) & 3;\n"
      "  .data : { *(.data) }\n"
      "}\n"),
                            "/tmp",
                            searchdirs,
                            "");
  EXPECT_TRUE(parser.Parse());

  EXPECT_EQ("", parser.startup());
  std::vector<string> expected_inputs;
  EXPECT_EQ(expected_inputs, parser.inputs());
  EXPECT_EQ("", parser.output());
}

TEST_F(LinkerScriptParserTest, ParseSample3) {
  std::vector<string> searchdirs;
  LinkerScriptParser parser(Content::CreateFromString(
      "OVERLAY 0x1000 : AT (0x4000)\n"
      " {\n"
      "  .text0 { o1/*.o(.text) }\n"
      "  .text1 { o2/*.o(.text) }\n"
      " }\n"),
                            "/tmp",
                            searchdirs,
                            "");
  EXPECT_TRUE(parser.Parse());

  EXPECT_EQ("", parser.startup());
  std::vector<string> expected_inputs;
  EXPECT_EQ(expected_inputs, parser.inputs());
  EXPECT_EQ("", parser.output());
}

}  // namespace devtools_goma
