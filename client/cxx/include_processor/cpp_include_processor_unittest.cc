// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "gtest/gtest.h"

#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_info.h"
#include "cpp_include_processor.h"
#include "cxx/cxx_compiler_info.h"
#include "file_stat_cache.h"
#include "filesystem.h"
#include "include_cache.h"
#include "include_file_finder.h"
#include "list_dir_cache.h"
#include "path.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

class CppIncludeProcessorTest : public testing::Test {
 public:
  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("include_processor_unittest");
    tmpdir_util_->SetCwd("");

    IncludeFileFinder::Init(true);
    ListDirCache::Init(4096);
  }

  void TearDown() override { ListDirCache::Quit(); }

  std::set<string> RunCppIncludeProcessor(const string& source_file,
                                          const std::vector<string>& args) {
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, tmpdir_util_->tmpdir()));
    std::unique_ptr<CompilerInfoData> data(new CompilerInfoData);
    data->set_found(true);
    data->mutable_cxx();
    CxxCompilerInfo compiler_info(std::move(data));

    CppIncludeProcessor processor;
    std::set<string> files;
    FileStatCache file_stat_cache;
    EXPECT_TRUE(processor.GetIncludeFiles(source_file, tmpdir_util_->tmpdir(),
                                          *flags, compiler_info, &files,
                                          &file_stat_cache));
    return files;
  }

  string CreateTmpFile(const string& content, const string& name) {
    tmpdir_util_->CreateTmpFile(name, content);
    return tmpdir_util_->FullPath(name);
  }

 protected:
  static void SetUpTestCase() {
    IncludeCache::Init(5, true);
  };

  static void TearDownTestCase() {
    IncludeCache::Quit();
  };

  std::unique_ptr<TmpdirUtil> tmpdir_util_;
  std::vector<string> env_;
};

TEST_F(CppIncludeProcessorTest, define_defined_with_paren) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define FOO\n"
      "#define DEFINED defined(FOO)\n"
      "#if DEFINED\n"
      "# include \"bar.h\"\n"
      "#endif\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    EXPECT_TRUE(files.empty());
  }
}

TEST_F(CppIncludeProcessorTest, define_defined_without_paren) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define FOO\n"
      "#define DEFINED defined FOO\n"
      "#if DEFINED\n"
      "# include \"bar.h\"\n"
      "#endif\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }
}

TEST_F(CppIncludeProcessorTest, comment_in_macro) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define BAR bar.h /**/\n"
      "#define STR_I(x) #x\n"
      "#define STR(x) STR_I(x)\n"
      "#include STR(BAR)\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }
}

TEST_F(CppIncludeProcessorTest, comment_in_func_macro) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define BAR(x) bar.h /**/\n"
      "#define STR_I(x) #x\n"
      "#define STR(x) STR_I(x)\n"
      "#include STR(BAR(hoge))\n",
      "foo.cc");
  string included = CreateTmpFile("", "bar.h");

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(included, *files.begin());
  }
}

TEST_F(CppIncludeProcessorTest, opt_include) {
  const string& header = CreateTmpFile("", "foo.h");
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-include");
  args.push_back(header);

  std::set<string> files =
      RunCppIncludeProcessor(CreateTmpFile("", "foo.c"), args);
  ASSERT_EQ(1U, files.size());
  EXPECT_EQ(header, *files.begin());
}

TEST_F(CppIncludeProcessorTest, opt_include_in_cwd) {
  CreateTmpFile("", "foo.h");
  std::vector<string> args;
  args.push_back("gcc");
  args.push_back("-include");
  args.push_back("foo.h");

  std::set<string> files =
      RunCppIncludeProcessor(CreateTmpFile("", "foo.c"), args);
  ASSERT_EQ(1U, files.size());
  EXPECT_EQ("foo.h", *files.begin());
}

TEST_F(CppIncludeProcessorTest, vc_opt_fi) {
  const string& header = CreateTmpFile("", "foo.h");
  std::vector<string> args;
  args.push_back("cl.exe");
  args.push_back("/c");
  args.push_back("/FI" + header);

  std::set<string> files =
      RunCppIncludeProcessor(CreateTmpFile("", "foo.c"), args);
  ASSERT_EQ(1U, files.size());
  ASSERT_EQ(header, *files.begin());
}

TEST_F(CppIncludeProcessorTest, no_newline_at_eof) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#if 1\n"
      "#include \"bar.h\"\n"
      "#include \"baz.h\"\n"
      "#endif\n",
      "foo.cc");
  string bar_h = CreateTmpFile(
      "#if 0\n"
      "#include \"hoge.h\"\n"
      "#endif",
      "bar.h");
  string baz_h = CreateTmpFile("", "baz.h");
  string hoge_h = CreateTmpFile("", "hoge.h");

  std::set<string> expected;
  expected.insert(bar_h);
  expected.insert(baz_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(2U, files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(2U, files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, no_newline_at_eof_identifier) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"foo.h\"\n"
      "#include \"bar.h\"\n"
      "#\n",
      "foo.cc");
  const string& foo_h = CreateTmpFile(
      "#define foo",  // No newline at the end after an identifier.
      "foo.h");
  const string& bar_h = CreateTmpFile(
      "#ifdef foo\n"
      "#include \"baz.h\"\n"
      "#endif\n",
      "bar.h");
  const string& baz_h = CreateTmpFile("", "baz.h");

  std::set<string> expected;
  expected.insert(foo_h);
  expected.insert(bar_h);
  expected.insert(baz_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, no_newline_at_eof_number) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"foo.h\"\n"
      "#define S(a) #a\n"
      "#define X(a) S(a.h)\n"
      "#include X(FOO)\n"
      "#\n",
      "foo.cc");
  const string& foo_h = CreateTmpFile(
      "#define FOO 999",  // No newline at the end after a pp-number.
      "foo.h");
  const string& nine_h = CreateTmpFile("", "999.h");

  std::set<string> expected;
  expected.insert(foo_h);
  expected.insert(nine_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, condition_lines_lf) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file = CreateTmpFile(
      "#define A 1\n"
      "#define B 1\n"
      "#if defined(A) && \\\n"
      "    defined(B)\n"
      "#include \"bar.h\"\n"
      "#endif\n",
      "foo.cc");
  string bar_h = CreateTmpFile("", "bar.h");

  std::set<string> expected;
  expected.insert(bar_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, condition_lines_crlf) {
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define A 1\r\n"
      "#define B 1\r\n"
      "#if defined(A) && \\\r\n"
      "    defined(B)\r\n"
      "#include \"bar.h\"\r\n"
      "#endif\\r\n",
      "foo.cc");
  string bar_h = CreateTmpFile("", "bar.h");

  std::set<string> expected;
  expected.insert(bar_h);
  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(1U, files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_cur_from_include_paths) {
  // b/7626343

  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file =
      CreateTmpFile("#include \"primpl.h\"\n", "foo.cc");
  const string& dir1 = "dir1";
  const string& nspr_h = file::JoinPath(dir1, "nspr.h");
  CreateTmpFile("", nspr_h);
  const string& dir2 = "dir2";
  const string& primpl_h = file::JoinPath(dir2, "primpl.h");
  CreateTmpFile("#include \"nspr.h\"\n", primpl_h);

  std::set<string> expected{nspr_h, primpl_h};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/I" + dir2);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_multiple_file) {
  // b/7461986
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file =
      CreateTmpFile("#include \"limits.h\"\n",  // limits_h_0
                    "foo.cc");
  const string& limits_h_0 =
      CreateTmpFile("#include_next \"limits.h\"\n",  // limits_h_1
                    "limits.h");
  const string& dir1 = "dir1";
  const string& limits_h_1 = file::JoinPath(dir1, "limits.h");
  CreateTmpFile(
      "#ifndef _LIBC_LIMITS_H\n"    // not defined yet
      "#include \"syslimits.h\"\n"  // so it should be included
      "#endif\n",
      limits_h_1);
  const string& syslimits_h = file::JoinPath(dir1, "syslimits.h");
  CreateTmpFile("", syslimits_h);
  const string& dir2 = "dir2";
  // If limits_h_2 is included (before limits_h_1), syslimits.h would not be
  // included.
  const string& limits_h_2 = CreateTmpFile("#define _LIBC_LIMITS_H\n",
                                           file::JoinPath(dir2, "limits.h"));

  ASSERT_NE(limits_h_1, limits_h_2);

  std::set<string> expected{
      limits_h_0, limits_h_1, syslimits_h,
  };

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_from_include_current_dir) {
  // b/7461986
  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file =
      CreateTmpFile("#include \"limits.h\"\n",  // include limits_h_0 (curdir)
                    "foo.cc");
  const string& limits_h_0 = CreateTmpFile(
      "#include_next <limits.h>\n",  // include limits_h_1 (first inc dir)
      "limits.h");
  const string& dir1 = "dir1";
  const string& limits_h_1 = file::JoinPath(dir1, "limits.h");
  CreateTmpFile(
      "#ifndef _LIBC_LIMITS_H\n"    // not defined yet
      "#include \"syslimits.h\"\n"  // so it should be included
      "#endif\n",
      limits_h_1);
  const string& syslimits_h = file::JoinPath(dir1, "syslimits.h");
  CreateTmpFile(
      "#include_next <limits.h>\n",  // include limits_h_2 (second inc dir)
      syslimits_h);
  const string& dir2 = "dir2";
  // If limits_h_2 is included from syslimits.h
  const string& limits_h_2 = file::JoinPath(dir2, "limits.h");
  CreateTmpFile("#define _LIBC_LIMITS_H\n", limits_h_2);

  ASSERT_NE(limits_h_1, limits_h_2);

  std::set<string> expected{
      limits_h_0, limits_h_1, syslimits_h, limits_h_2,
  };

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_from_next_dir) {
  // b/7462563

  const string& bare_gcc = "/usr/bin/g++";
  const string& source_file =
      CreateTmpFile("#include <_clocale.h>\n",  // clocate_h
                    "foo.cc");
  const string& dir1 = "dir1";
  const string& clocale_h = file::JoinPath(dir1, "_clocale.h");
  CreateTmpFile("#include_next <clocale>\n",  // include clocale_2
                clocale_h);
  const string& clocale_1 = CreateTmpFile("", file::JoinPath(dir1, "clocale"));
  const string& dir2 = "dir2";
  const string& clocale_2 = file::JoinPath(dir2, "clocale");
  CreateTmpFile("", clocale_2);

  ASSERT_NE(clocale_1, clocale_2);

  std::set<string> expected{clocale_h, clocale_2};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, invalidated_macro_in_offspring) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#define var1\n"
      "#include \"step1.h\"\n"
      "#include \"step1.h\"\n"
      "#\n",
      "foo.cc");
  const string& step1_h = CreateTmpFile(
      "#include \"step2.h\"\n"
      "#undef var1\n",
      "step1.h");
  const string& step2_h = CreateTmpFile(
      "#if !defined var1\n"
      "#define var2\n"
      "#endif\n"
      "\n"
      "#ifdef var2\n"
      "#include \"step3.h\"\n"
      "#endif\n",
      "step2.h");
  const string& step3_h = CreateTmpFile("\n", "step3.h");

  std::set<string> expected;
  expected.insert(step1_h);
  expected.insert(step2_h);
  expected.insert(step3_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_ignore_dir) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile("#include \"string\"\n", "foo.cc");
  const string& string_dir = "string";
  CHECK(file::CreateDir(tmpdir_util_->FullPath(string_dir).c_str(),
                        file::CreationMode(0777))
            .ok());
  const string& dir1 = "dir1";
  const string& string_h = file::JoinPath(dir1, "string");
  CreateTmpFile("", string_h);

  std::set<string> expected{string_h};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_ignore_dir) {
  const string& bare_gcc = "/usr/bin/gcc";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile("#include <foo.h>\n", "foo.cc");
  const string& dir1 = "dir1";
  const string& foo_h = file::JoinPath(dir1, "foo.h");
  CreateTmpFile("#include <string>\n", foo_h);
  const string& string1 = file::JoinPath(dir1, "string");
  CreateTmpFile("#include_next <string>\n", string1);
  const string& dir2 = "dir2";
  const string& dir3 = "dir3";
  const string& string3 = file::JoinPath(dir3, "string");
  CreateTmpFile("", string3);

  std::set<string> expected{
      foo_h, string1, string3,
  };

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-I" + dir2);
    args.push_back("-I" + dir3);
    args.push_back("-c");
    args.push_back(source_file);
    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/I" + dir2);
    args.push_back("/I" + dir3);
    args.push_back("/c");
    args.push_back(source_file);
    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_path_two_slashes_in_dir_cache) {
  // b/7618390

  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"dir2//foo.h\"\n"        // foo_h
      "#include \"dir3//dir4//bar.h\"\n"  // bar_h
      "#include \"dir3/dir4/baz.h\"\n",   // baz_h
      "foo.cc");
  const string& dir1 = "dir1";
  const string& dir2 = file::JoinPath(dir1, "dir2");
  const string& foo_h = file::JoinPath(dir2, "foo.h");
  CreateTmpFile("", foo_h);
  const string& dir3 = file::JoinPath(dir1, "dir3");
  const string& dir4 = file::JoinPath(dir3, "dir4");
  const string& bar_h = file::JoinPath(dir4, "bar.h");
  CreateTmpFile("", bar_h);
  const string& baz_h = file::JoinPath(dir4, "baz.h");
  CreateTmpFile("", baz_h);

  std::set<string> expected{foo_h, bar_h, baz_h};

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_unresolved_path) {
  const string& bare_gcc = "/usr/bin/g++";
  const string& bare_cl = "cl.exe";
  const string& source_file = CreateTmpFile(
      "#include \"dir2/../foo.h\"\n"         // foo_h
      "#include \"dir2//../hoge.h\"\n"       // hoge_h
      "#include \"dir3/../dir4/bar.h\"\n"    // bar_h
      "#include \"dir3/..//dir4/baz.h\"\n",  // baz_h
      "foo.cc");
  const string& dir1 = "dir1";
  const string& full_dir1 = file::JoinPath(tmpdir_util_->tmpdir(), dir1);
  CHECK(file::CreateDir(full_dir1.c_str(), file::CreationMode(0777)).ok());
  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& hoge_h = CreateTmpFile("", file::JoinPath(dir1, "hoge.h"));
  const string& dir2 = file::JoinPath(dir1, "dir2");
  const string& full_dir2 = file::JoinPath(tmpdir_util_->tmpdir(), dir2);
  CHECK(file::CreateDir(full_dir2.c_str(), file::CreationMode(0777)).ok());
  const string& unresolved_foo_h =
      file::JoinPath(file::JoinPath(dir2, ".."), "foo.h");
  ASSERT_NE(unresolved_foo_h, foo_h);
  const string& unresolved_hoge_h =
      file::JoinPath(file::JoinPath(dir2, ".."), "hoge.h");
  ASSERT_NE(unresolved_hoge_h, hoge_h);
  const string& dir3 = file::JoinPath(dir1, "dir3");
  const string& full_dir3 = file::JoinPath(tmpdir_util_->tmpdir(), dir3);
  CHECK(file::CreateDir(full_dir3.c_str(), file::CreationMode(0777)).ok());
  const string& dir4 = file::JoinPath(dir1, "dir4");
  CHECK(file::CreateDir(file::JoinPath(tmpdir_util_->tmpdir(), dir4).c_str(),
                        file::CreationMode(0777))
            .ok());
  const string& bar_h = CreateTmpFile("", file::JoinPath(dir4, "bar.h"));
  const string& baz_h = CreateTmpFile("", file::JoinPath(dir4, "baz.h"));
  const string& unresolved_bar_h = file::JoinPath(
      file::JoinPath(file::JoinPath(dir3, ".."), "dir4"), "bar.h");
  ASSERT_NE(unresolved_bar_h, bar_h);
  const string& unresolved_baz_h = file::JoinPath(
      file::JoinPath(file::JoinPath(dir3, ".."), "dir4"), "baz.h");
  ASSERT_NE(unresolved_baz_h, baz_h);

  std::set<string> expected;
  expected.insert(unresolved_foo_h);
  expected.insert(unresolved_hoge_h);
  expected.insert(unresolved_bar_h);
  expected.insert(unresolved_baz_h);

  {
    std::vector<string> args;
    args.push_back(bare_gcc);
    args.push_back("-I" + dir1);
    args.push_back("-c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/I" + dir1);
    args.push_back("/c");
    args.push_back(source_file);

    std::set<string> files = RunCppIncludeProcessor(source_file, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, newline_before_include) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc =
      CreateTmpFile("\n#include \"foo.h\"", file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, newline_and_spaces_before_include) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile("f();   \n   #include \"foo.h\"",
                                       file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, noncomment_token_before_include) {
  const string& dir1 = "dir1";

  CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile("f(); \t   #include \"foo.h\"",
                                       file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, comment_slash_followed_by_include_simple) {
  const string& dir1 = "dir1";

  const string& foo1_h = CreateTmpFile("", file::JoinPath(dir1, "foo1.h"));
  const string& foo2_h = CreateTmpFile("", file::JoinPath(dir1, "foo2.h"));
  const string& foo_cc = CreateTmpFile(
      "   \\\n#include \"foo1.h\"\n  /* test */ \\\n#include \"foo2.h\"",
      file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo1_h);
  expected.insert(foo2_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, comment_slash_followed_by_include_complex1) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile(
      "  /* test */ \\\r\n /* test 2 */ /* */ \\\n"
      "\\\n /* foo bar */ \\\n#include \"foo.h\"",
      file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, comment_slash_followed_by_include_complex2) {
  const string& dir1 = "dir1";

  const string& foo_h = CreateTmpFile("", file::JoinPath(dir1, "foo.h"));
  const string& foo_cc = CreateTmpFile(
      "#define FOO \"foo.h\"\n"
      "  /* test */ \\\r\n /* test 2 */ /* */ \\\n"
      "\\\n /* foo bar */ \\\n#include FOO",
      file::JoinPath(dir1, "foo.cc"));

  std::vector<string> args;
  args.push_back("/usr/bin/g++");
  args.push_back("-c");
  args.push_back("-I" + dir1);
  args.push_back(foo_cc);

  std::set<string> expected;
  expected.insert(foo_h);

  std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_boost_pp_iterate) {
  const string& foo_cc = CreateTmpFile(
      // simplified case for BOOST_PP_ITERATE
      // cf. b/14593802
      // <boost/preprocessor/cat.hpp>
      "#define CAT(a, b) CAT_I(a, b)\n"
      "#define CAT_I(a, b) CAT_II(~, a ## b)\n"
      "#define CAT_II(p, res) res\n"
      // <boost/preprocessor/arithmetic/inc.cpp>
      "#define INC(x) INC_I(x)\n"
      "#define INC_I(x) INC_ ## x\n"
      "#define INC_0 1\n"
      "#define INC_1 2\n"
      // <boost/preprocessor/iteration/iterate.hpp>
      "#define DEPTH() 0\n"
      "\n"
      "#define ITERATE() CAT(ITERATE_, INC(DEPTH()))\n"
      "#define ITERATE_1 <bar1.h>\n"
      "#define ITERATE_2 <bar2.h>\n"
      // use ITERATE
      "#include ITERATE()\n",
      "foo.cc");
  CreateTmpFile("", "bar1.h");
  CreateTmpFile("", "bar2.h");
  std::set<string> expected{file::JoinPath(".", "bar1.h")};

  {
    std::vector<string> args;
    args.push_back("/usr/bin/g++");
    args.push_back("-c");
    args.push_back("-I.");
    args.push_back(foo_cc);

    std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }

  {
    std::vector<string> args;
    args.push_back("cl.exe");
    args.push_back("/c");
    args.push_back("/I.");
    args.push_back(foo_cc);

    std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_boost_pp_iterate_va_args) {
  const string& foo_cc = CreateTmpFile(
      // simplified case for BOOST_PP_ITERATE
      // cf.
      // boost v1.49.0
      // TODO: MSVC has slightly different semantics in __VA_ARGS__,
      // one more BOOST_PP_CAT needed?
      // e.g. #define BOOST_PP_VARIADIC_SIZE(...) \
      //  BOOST_PP_CAT(BOOST_PP_VARIADIC_SIZE_I(<same>),)
      // <boost/preprocessor/cat.hpp>
      "#define BOOST_PP_CAT(a, b) BOOST_PP_CAT_I(a, b)\n"
      "#define BOOST_PP_CAT_I(a, b) a ## b\n"
      // <boost/preprocessor/tuple/rem.hpp>
      "#define BOOST_PP_REM(...) __VA_ARGS__\n"
      // <boost/preprocessor/variadic/size.hpp>
      "#define BOOST_PP_VARIADIC_SIZE(...) "
      " BOOST_PP_VARIADIC_SIZE_I(__VA_ARGS__, 64, 63, 62, 61, 60, 59, 58, 57,"
      " 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41,40,"
      " 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23,"
      " 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,"
      " 4, 3, 2, 1,)\n"
      "#define BOOST_PP_VARIADIC_SIZE_I(e0, e1, e2, e3, e4, e5, e6, e7, e8,"
      " e9, e10, e11, e12, e13, e14, e15, e16, e17, e18, e19, e20, e21, e22,"
      " e23, e24, e25, e26, e27, e28, e29, e30, e31, e32, e33, e34, e35, e36,"
      " e37, e38, e39, e40, e41, e42, e43, e44, e45, e46, e47,e48, e49, e50,"
      " e51, e52, e53, e54, e55, e56, e57, e58, e59, e60, e61, e62, e63,"
      " size, ...) size\n"
      // <boost/preprocessor/facilities/overload.hpp>
      "#define BOOST_PP_OVERLOAD(prefix, ...) "
      " BOOST_PP_CAT(prefix, BOOST_PP_VARIADIC_SIZE(__VA_ARGS__))\n"
      // <boost/preprocessor/variadic/elem.hpp>
      "#define BOOST_PP_VARIADIC_ELEM(n, ...) "
      " BOOST_PP_CAT(BOOST_PP_VARIADIC_ELEM_, n)(__VA_ARGS__,)\n"
      "#define BOOST_PP_VARIADIC_ELEM_0(e0, ...) e0\n"
      "#define BOOST_PP_VARIADIC_ELEM_1(e0, e1, ...) e1\n"
      "#define BOOST_PP_VARIADIC_ELEM_2(e0, e1, e2, ...) e2\n"
      // <boost/preprocessor/tuple/elem.hpp>
      "#define BOOST_PP_TUPLE_ELEM(...) "
      "  BOOST_PP_OVERLOAD(BOOST_PP_TUPLE_ELEM_O_, __VA_ARGS__)(__VA_ARGS__)\n"
      "#define BOOST_PP_TUPLE_ELEM_O_2(n, tuple) "
      " BOOST_PP_VARIADIC_ELEM(n, BOOST_PP_REM tuple)\n"
      "#define BOOST_PP_TUPLE_ELEM_O_3(size, n, tuple) "
      " BOOST_PP_TUPLE_ELEM_O_2(n, tuple)\n"
      // <boost/preprocessor/array/size.hpp>
      "#define BOOST_PP_ARRAY_SIZE(array) BOOST_PP_TUPLE_ELEM(2, 0, array)\n"
      // <boost/preprocessor/array/data.hpp>
      "#define BOOST_PP_ARRAY_DATA(array) BOOST_PP_TUPLE_ELEM(2, 1, array)\n"
      // <boost/preprocessor/array/elem.hpp>
      "#define BOOST_PP_ARRAY_ELEM(i, array) "
      " BOOST_PP_TUPLE_ELEM(BOOST_PP_ARRAY_SIZE(array), i,"
      " BOOST_PP_ARRAY_DATA(array))\n"
      // <boost/utility/result_of.hpp>
      "#define BOOST_RESULT_OF_NUM_ARGS 10\n"
      "#define BOOST_PP_ITERATION_PARAMS_1 "
      " (3,(0,BOOST_RESULT_OF_NUM_ARGS,<bar1.h>))\n"
      // <boost/preprocessor/iteration/detail/iter/forward1.hpp>
      "#define BOOST_PP_FILENAME_1 "
      "  BOOST_PP_ARRAY_ELEM(2, BOOST_PP_ITERATION_PARAMS_1)\n"
      "#define BOOST_PP_ITERATION_1 0\n"
      "#include BOOST_PP_FILENAME_1\n",
      "foo.cc");
  CreateTmpFile("", "bar1.h");
  std::set<string> expected{file::JoinPath(".", "bar1.h")};
  {
    std::vector<string> args;
    args.push_back("/usr/bin/g++");
    args.push_back("-c");
    args.push_back("-I.");
    args.push_back(foo_cc);

    std::set<string> files = RunCppIncludeProcessor(foo_cc, args);
    ASSERT_EQ(expected.size(), files.size());
    EXPECT_EQ(expected, files);
  }
}

TEST_F(CppIncludeProcessorTest, include_next_self) {
  const string& bare_gcc = "/usr/bin/g++";

  const string& source_file = CreateTmpFile("#include \"a.h\"\n", "a.cc");
  const string& ah = CreateTmpFile("#include_next <a.h>\n", "a.h");

  const string& aah = file::JoinPath("a", "a.h");
  CreateTmpFile("", aah);

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-I.");
  args.push_back("-Ia");
  args.push_back("-c");
  args.push_back(source_file);

  std::set<string> expected{ah, file::JoinPath(".", "a.h"), aah};

  std::set<string> files = RunCppIncludeProcessor(source_file, args);
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_quote_from_current) {
  const string& bare_gcc = "/usr/bin/g++";

  const string& source_file =
      CreateTmpFile("#include \"a.h\"\n", file::JoinPath("a", "a.cc"));
  const string& aah = CreateTmpFile("", file::JoinPath("a", "a.h"));

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-c");
  args.push_back(source_file);

  std::set<string> expected;
  expected.insert(aah);

  std::set<string> files = RunCppIncludeProcessor(source_file, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_sibling) {
  const string& bare_gcc = "/usr/bin/g++";

  const string& source_file =
      CreateTmpFile("#include \"../b/b.h\"\n", file::JoinPath("a", "a.cc"));
  const string& bbh = CreateTmpFile("", file::JoinPath("a", "..", "b", "b.h"));

  std::vector<string> args;
  args.push_back(bare_gcc);
  args.push_back("-c");
  args.push_back(source_file);

  std::set<string> expected;
  expected.insert(bbh);

  std::set<string> files = RunCppIncludeProcessor(source_file, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_from_dir) {
  const string& ac = file::JoinPath("test", "a.c");
  CreateTmpFile("#include \"a.h\"\n", ac);

  const string& ah = file::JoinPath("test", "a.h");
  CreateTmpFile("", ah);

  std::vector<string> args{"/usr/bin/gcc", "-c", ac};
  std::set<string> expected{ah};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_from_dir_in_include_dir) {
  const string& ac = "a.c";
  CreateTmpFile("#include <test/a.h>\n", ac);

  const string& ah = file::JoinPath(".", "test", "a.h");
  CreateTmpFile("#include \"b.h\"", ah);

  const string& bh = file::JoinPath(".", "test", "b.h");
  CreateTmpFile("", bh);

  std::vector<string> args{"/usr/bin/gcc", "-I.", "-c", ac};
  std::set<string> expected{ah, bh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_from_abs_rel_include_dir) {
  const string& ac = "a.c";
  CreateTmpFile(
      "#include <abs.h>\n"
      "#include <rel.h>\n",
      ac);

  const string& relh = file::JoinPath("rel", "rel.h");
  CreateTmpFile("", relh);

  const string& absh = CreateTmpFile("", file::JoinPath("abs", "abs.h"));

  std::vector<string> args{"/usr/bin/gcc", "-Irel",
                           "-I" + tmpdir_util_->FullPath("abs"), "-c", ac};
  std::set<string> expected{relh, absh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  ASSERT_EQ(expected.size(), files.size());
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, include_guard_once_alias) {
  const string& ac = file::JoinPath("a", "a.c");
  CreateTmpFile("#include \"../b/b.h\"\n", ac);

  const string& bh = file::JoinPath("a", "..", "b", "b.h");
  CreateTmpFile(
      "#pragma once\n"
      "#include \"../b/b.h\"\n",
      bh);

  std::vector<string> args{"/usr/bin/gcc", "-c", ac};
  std::set<string> expected{bh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  EXPECT_EQ(expected, files);
}

TEST_F(CppIncludeProcessorTest, undef_content) {
  const string& inc = file::JoinPath(".", "inc.h");
  CreateTmpFile(
      "#define THIS FILE\n"
      "#include THIS\n"
      "#undef THIS\n",
      inc);

  const string& ac = file::JoinPath(".", "a.c");
  CreateTmpFile(
      "#define FILE \"a.h\"\n"
      "#include \"inc.h\"\n"
      "#undef FILE\n"
      "#define FILE \"b.h\"\n"
      "#include \"inc.h\"\n",
      ac);

  const string& ah = file::JoinPath(".", "a.h");
  const string& bh = file::JoinPath(".", "b.h");
  CreateTmpFile("", ah);
  CreateTmpFile("", bh);

  std::vector<string> args{"/usr/bin/gcc", "-c", ac};
  std::set<string> expected{inc, ah, bh};

  std::set<string> files = RunCppIncludeProcessor(ac, args);
  EXPECT_EQ(expected, files);
}

}  // namespace devtools_goma
