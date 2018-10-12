// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/flag_parser.h"

#include "glog/stl_logging.h"
#include "gtest/gtest.h"
using std::string;

class AddFramework : public FlagParser::Callback {
 public:
  string ParseFlagValue(
      const FlagParser::Flag& /* flag */, const string& value) override {
    return value + " (framework)";
  }
};

TEST(FlagParserTest, Parse) {
  FlagParser parser;
  // Same options as GCCFlags::DefineFlags().
  // Don't want to introduce dependency to compiler_flags here.
  parser.mutable_options()->flag_prefix = '-';
  parser.mutable_options()->allows_equal_arg = true;
  parser.mutable_options()->allows_nonspace_arg = true;

  bool E, c;
  parser.AddBoolFlag("E")->SetSeenOutput(&E);
  parser.AddBoolFlag("c")->SetSeenOutput(&c);

  FlagParser::Flag* flag_arch = parser.AddFlag("arch");
  FlagParser::Flag* flag_x = parser.AddFlag("x");
  FlagParser::Flag* flag_o = parser.AddFlag("o");
  FlagParser::Flag* flag_isysroot = parser.AddFlag("isysroot");
  FlagParser::Flag* flag_WpMD = parser.AddFlag("Wp,MD,");
  FlagParser::Flag* flag_MF = parser.AddFlag("MF");
  FlagParser::Flag* flag_O = parser.AddPrefixFlag("O");

  AddFramework add_framework;
  std::vector<string> I;
  parser.AddFlag("I")->SetValueOutputWithCallback(nullptr, &I);
  parser.AddFlag("F")->SetValueOutputWithCallback(&add_framework, &I);

  FlagParser::Flag* flag_D = parser.AddFlag("D");

  std::vector<string> include_related;
  parser.AddFlag("include")->SetOutput(&include_related);
  parser.AddFlag("isystem")->SetOutput(&include_related);
  parser.AddFlag("B")->SetOutput(&include_related);

  FlagParser::Flag* non_flag = parser.AddNonFlag();

  std::vector<string> args;
  // The name of command.
  args.push_back("/Users/goma/goma/gcc");

  // A switch without an argument.
  args.push_back("-c");

  // We support three types of switches with arguments.
  args.push_back("-xc++");
  args.push_back("-arch");
  args.push_back("i386");
  args.push_back("-isysroot=/Developer/SDKs/MacOSX10.5.sdk");

  // The "foobar" must not appear in input_files.
  args.push_back("-MF");
  args.push_back("foobar");

  // We can handle this case as well.
  args.push_back("-Wp,MD,animation.dep");

  // Multiple values for the same switch.
  args.push_back("-I../skia/ext");
  args.push_back("-I../third_party/libjpeg");
  args.push_back("-Ffoo.framework/Frameworks");
  args.push_back("-I../third_party/libpng");

  // We should keep original arguments for them.
  args.push_back("-include");
  args.push_back("foo.h");
  args.push_back("-isystem=foo");
  args.push_back("-Bbar");

  args.push_back("-DFOO");
  // -DBAR=BAZ should be parsed as {"D": "BAR=BAZ"}, not {"DBAR": "BAZ"}.
  args.push_back("-DBAR=BAZ");

  // Unknown flags
  args.push_back("-fmessage-length=0");
  args.push_back("-pipe");
  args.push_back("-fno-exceptions");
  args.push_back("-Wall");

  // flag_O will be -O0, -Os and -O. Make sure -O should not take next argument.
  args.push_back("-O0");
  args.push_back("-Os");
  args.push_back("-O");

  // An argument without a leading switch.
  args.push_back("/Users/goma/gitchr/src/app/animation_container.cc");
  // This should be treated as an input.
  args.push_back("-");

  // Error case: the argument is missing. We ignore this flag.
  args.push_back("-o");

  parser.Parse(args);

  EXPECT_FALSE(E);
  EXPECT_TRUE(c);

  EXPECT_EQ("i386", flag_arch->GetLastValue());
  EXPECT_EQ("c++", flag_x->GetLastValue());
  EXPECT_EQ("", flag_o->GetLastValue());
  EXPECT_EQ("/Developer/SDKs/MacOSX10.5.sdk", flag_isysroot->GetLastValue());
  EXPECT_EQ("animation.dep", flag_WpMD->GetLastValue());
  EXPECT_EQ("foobar", flag_MF->GetLastValue());

  ASSERT_EQ(4U, I.size());
  EXPECT_EQ("../skia/ext", I[0]);
  EXPECT_EQ("../third_party/libjpeg", I[1]);
  EXPECT_EQ("foo.framework/Frameworks (framework)", I[2]);
  EXPECT_EQ("../third_party/libpng", I[3]);

  ASSERT_EQ(2U, flag_D->values().size());
  EXPECT_EQ("FOO", flag_D->value(0));
  EXPECT_EQ("BAR=BAZ", flag_D->value(1));

  ASSERT_EQ(4U, include_related.size());
  EXPECT_EQ("-include", include_related[0]);
  EXPECT_EQ("foo.h", include_related[1]);
  EXPECT_EQ("-isystem=foo", include_related[2]);
  EXPECT_EQ("-Bbar", include_related[3]);

  ASSERT_EQ(3U, flag_O->values().size());
  EXPECT_EQ("0", flag_O->value(0));
  EXPECT_EQ("s", flag_O->value(1));
  EXPECT_EQ("", flag_O->value(2));

  ASSERT_EQ(2U, non_flag->values().size());
  EXPECT_EQ("/Users/goma/gitchr/src/app/animation_container.cc",
            non_flag->value(0));
  EXPECT_EQ("-", non_flag->value(1));

  ASSERT_EQ(5U, parser.unknown_flag_args().size())
      << parser.unknown_flag_args();
  EXPECT_EQ("-fmessage-length=0", parser.unknown_flag_args()[0]);
  EXPECT_EQ("-pipe", parser.unknown_flag_args()[1]);
  EXPECT_EQ("-fno-exceptions", parser.unknown_flag_args()[2]);
  EXPECT_EQ("-Wall", parser.unknown_flag_args()[3]);
  // -o is missing argument, so counted as unknown flags.
  EXPECT_EQ("-o", parser.unknown_flag_args()[4]);
}

TEST(FlagParserTest, ParseBoolFlag) {
  FlagParser parser;
  parser.mutable_options()->flag_prefix = '-';
  parser.mutable_options()->allows_equal_arg = true;
  parser.mutable_options()->allows_nonspace_arg = true;

  bool c;
  parser.AddBoolFlag("c")->SetSeenOutput(&c);

  std::vector<string> args;
  args.push_back("x86_65-cros-linux-gnu-gcc");
  args.push_back("-clang-syntax");

  parser.Parse(args);
  EXPECT_FALSE(c);
}

TEST(FlagParserTest, AltPrefix) {
  FlagParser parser;
  parser.mutable_options()->flag_prefix = '/';
  parser.mutable_options()->alt_flag_prefix = '-';
  parser.mutable_options()->allows_nonspace_arg = true;

  FlagParser::Flag* flag_D = parser.AddFlag("D");
  FlagParser::Flag* non_flag = parser.AddNonFlag();

  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("-DFOO=BAR");
  args.push_back("/DBAZ");

  args.push_back("foo.cc");

  parser.Parse(args);
  ASSERT_EQ(2UL, flag_D->values().size());
  EXPECT_EQ("FOO=BAR", flag_D->value(0));
  EXPECT_EQ("BAZ", flag_D->value(1));

  ASSERT_EQ(1UL, non_flag->values().size());
  EXPECT_EQ("foo.cc", non_flag->value(0));
}

TEST(FlagParserTest, WeakAltPrefix) {
  FlagParser parser;
  parser.mutable_options()->flag_prefix = '-';
  parser.mutable_options()->alt_flag_prefix = '/';
  parser.mutable_options()->allows_nonspace_arg = true;

  FlagParser::Flag* flag_D = parser.AddFlag("D");
  FlagParser::Flag* non_flag = parser.AddNonFlag();

  std::vector<string> args;
  args.push_back("clang-cl");
  args.push_back("-DFOO=BAR");
  args.push_back("/DBAZ");

  // since '/' is alt_flag_prefix, and we didn't add any flag that starts
  // with 'h', it will be considered as non flag arg.
  args.push_back("/home/foo/src/foo.cc");

  parser.Parse(args);
  ASSERT_EQ(2UL, flag_D->values().size());
  EXPECT_EQ("FOO=BAR", flag_D->value(0));
  EXPECT_EQ("BAZ", flag_D->value(1));

  ASSERT_EQ(1UL, non_flag->values().size());
  EXPECT_EQ("/home/foo/src/foo.cc", non_flag->value(0));
}

// We actually won't have this case to support clang-cl used on Linux
// while sharing the code with Windows.
TEST(FlagParserTest, ClexeUnknownFlagsAltPrefix) {
  FlagParser parser;
  parser.mutable_options()->flag_prefix = '/';
  parser.mutable_options()->alt_flag_prefix = '-';
  parser.mutable_options()->allows_nonspace_arg = true;

  parser.AddFlag("D");

  std::vector<string> args {
    "clang-cl",
    "-DFOO=BAR",
    "/DBAZ",
    "/UNKNOWN",  // unknown flag.
    "/home/foo/src/foo.cc",  // unknown flag.
  };

  parser.Parse(args);

  ASSERT_EQ(2U, parser.unknown_flag_args().size())
      << parser.unknown_flag_args();
  EXPECT_EQ("/UNKNOWN", parser.unknown_flag_args()[0]);
  EXPECT_EQ("/home/foo/src/foo.cc", parser.unknown_flag_args()[1]);
}

TEST(FlagParserTest, ClexeUnknownFlagsWeakAltPrefix) {
  FlagParser parser;
  parser.mutable_options()->flag_prefix = '-';
  parser.mutable_options()->alt_flag_prefix = '/';
  parser.mutable_options()->allows_nonspace_arg = true;

  parser.AddFlag("D");

  std::vector<string> args {
    "clang-cl",
    "-DFOO=BAR",
    "/DBAZ",
    "/UNKNOWN",  // this is considered as non flag (!= unknown flag)
    "/home/foo/src/foo.cc",  // this, too.
  };

  parser.Parse(args);

  ASSERT_EQ(0U, parser.unknown_flag_args().size())
      << parser.unknown_flag_args();
}
