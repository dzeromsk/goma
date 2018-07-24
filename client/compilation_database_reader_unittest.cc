// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <gtest/gtest.h>
#include <json/json.h>

#include "compilation_database_reader.h"
#include "file_dir.h"
#include "file_helper.h"
#include "path.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

class CompilationDatabaseReaderTest : public testing::Test {
 protected:
  static bool AddCompileOptions(const std::string& source,
                                const std::string& db_path,
                                std::vector<std::string>* clang_args,
                                string* build_dir) {
    return CompilationDatabaseReader::AddCompileOptions(
        source, db_path, clang_args, build_dir);
  }

  static bool MakeClangArgsFromCommandLine(
      bool seen_hyphen_hyphen,
      const std::vector<string>& args_after_hyphen_hyphen,
      const string& input_file,
      const string& cwd,
      const string& build_path,
      const std::vector<string>& extra_arg,
      const std::vector<string>& extra_arg_before,
      const string& compdb_path,
      std::vector<string>* clang_args,
      string* build_dir) {
    return CompilationDatabaseReader::MakeClangArgsFromCommandLine(
        seen_hyphen_hyphen, args_after_hyphen_hyphen, input_file, cwd,
        build_path, extra_arg, extra_arg_before, compdb_path, clang_args,
        build_dir);
  }

  static string MakeCompilationDatabaseContent(const string& directory,
                                               const string& command,
                                               const string& file) {
    Json::Value comp;
    comp["directory"] = directory;
    comp["command"] = command;
    comp["file"] = file;

    Json::Value root;
    root.append(comp);

    Json::FastWriter writer;
    return writer.write(root);
  }
};

TEST_F(CompilationDatabaseReaderTest, FindCompilationDatabase) {
  TmpdirUtil tmpdir("compdb_unittest_fcd");
  tmpdir.SetCwd("/");

  string ab_rel = file::JoinPath("a", "b");
  string ab_abs = tmpdir.FullPath(ab_rel);

  string compdb_content = MakeCompilationDatabaseContent(
      ab_abs,
      "clang -IA -IB -c foo.cc",
      "foo.cc");

  // The following directories and file are created.
  // /a/b/
  // /c/d/
  //   /compile_commands.json

  tmpdir.MkdirForPath(ab_rel, true);
  tmpdir.MkdirForPath(file::JoinPath("c", "d"), true);
  tmpdir.CreateTmpFile(file::JoinPath("c", "compile_commands.json"),
                       compdb_content);

  const string c_abs = tmpdir.FullPath("c");
  const string cd_abs = tmpdir.FullPath(file::JoinPath("c", "d"));
  const string expected_compdb_path =
      file::JoinPath(c_abs, "compile_commands.json");

  // Set build_path is /c, first input file dir is /a/b
  {
    string compdb_path =
        CompilationDatabaseReader::FindCompilationDatabase(c_abs, ab_abs);
    EXPECT_EQ(expected_compdb_path, compdb_path);
  }

  // Set build_path is empty, first input file dir is /c/d.
  {
    string compdb_path =
        CompilationDatabaseReader::FindCompilationDatabase(string(), cd_abs);
    EXPECT_EQ(expected_compdb_path, compdb_path);
  }

  // Set build_path id /c/d, first input file dir is /a/b.
  // Since we shouldn't search ancestor directory of build_path,
  // compilation database should not be found.
  {
    string dbpath =
        CompilationDatabaseReader::FindCompilationDatabase(cd_abs, ab_abs);
    EXPECT_TRUE(dbpath.empty());
  }
}

TEST_F(CompilationDatabaseReaderTest, WithCompilationDatabase) {
  TmpdirUtil tmpdir("compdb_unittest");
  tmpdir.SetCwd("/");

  // Make the following directories and files, and move cwd to /a/b.
  // /a/b/
  // /compile_commands.json

  string ab_rel = file::JoinPath("a", "b");
  string ab_abs = tmpdir.FullPath(ab_rel);

  tmpdir.MkdirForPath(ab_rel, true);

  string compdb_content = MakeCompilationDatabaseContent(
      ab_abs,
      "clang -IA -IB -c foo.cc",
      "foo.cc");
  tmpdir.CreateTmpFile("compile_commands.json", compdb_content);

  string compile_commands_json = tmpdir.FullPath("compile_commands.json");

  string dbpath =
      CompilationDatabaseReader::FindCompilationDatabase("", ab_abs);
  EXPECT_EQ(compile_commands_json, dbpath);

  tmpdir.SetCwd(ab_rel);
  string foo_path = tmpdir.FullPath("foo.cc");

  std::vector<string> clang_args {
    "clang++",
  };
  string build_dir;
  EXPECT_TRUE(AddCompileOptions(foo_path, dbpath, &clang_args, &build_dir));

  std::vector<string> expected_clang_args {
    "clang++", "-IA", "-IB", "-c", "foo.cc"
  };
  EXPECT_EQ(expected_clang_args, clang_args);
  EXPECT_EQ(ab_abs, build_dir);
}

TEST_F(CompilationDatabaseReaderTest, WithCompilationDatabaseHavingGomaCC) {
  TmpdirUtil tmpdir("compdb_unittest");
  tmpdir.SetCwd("/");

  // Make the following directories and files, and move cwd to /a/b.
  // /a/b/
  // /compile_commands.json

  string ab_rel = file::JoinPath("a", "b");
  string ab_abs = tmpdir.FullPath(ab_rel);

  tmpdir.MkdirForPath(ab_rel, true);

  string compdb_content = MakeCompilationDatabaseContent(
      ab_abs,
      "/home/goma/goma/gomacc clang -IA -IB -c foo.cc",
      "foo.cc");
  tmpdir.CreateTmpFile("compile_commands.json", compdb_content);

  string compile_commands_json = tmpdir.FullPath("compile_commands.json");

  string dbpath =
      CompilationDatabaseReader::FindCompilationDatabase("", ab_abs);
  EXPECT_EQ(compile_commands_json, dbpath);

  tmpdir.SetCwd(ab_rel);
  string foo_path = tmpdir.FullPath("foo.cc");

  std::vector<string> clang_args {
    "clang++",
  };
  string build_dir;
  EXPECT_TRUE(AddCompileOptions(foo_path, dbpath, &clang_args, &build_dir));

  std::vector<string> expected_clang_args {
    "clang++", "-IA", "-IB", "-c", "foo.cc"
  };
  EXPECT_EQ(expected_clang_args, clang_args);
  EXPECT_EQ(ab_abs, build_dir);
}

TEST_F(CompilationDatabaseReaderTest,
       WithCompilationDatabaseHavingGomaCCCapitalCaseWithExtension) {
  TmpdirUtil tmpdir("compdb_unittest");
  tmpdir.SetCwd("/");

  // Make the following directories and files, and move cwd to /a/b.
  // /a/b/
  // /compile_commands.json

  string ab_rel = file::JoinPath("a", "b");
  string ab_abs = tmpdir.FullPath(ab_rel);

  tmpdir.MkdirForPath(ab_rel, true);

  string compdb_content = MakeCompilationDatabaseContent(
      ab_abs,
      "/home/goma/goma/GOMACC.exe clang -IA -IB -c foo.cc",
      "foo.cc");
  tmpdir.CreateTmpFile("compile_commands.json", compdb_content);

  string compile_commands_json = tmpdir.FullPath("compile_commands.json");

  string dbpath =
      CompilationDatabaseReader::FindCompilationDatabase("", ab_abs);
  EXPECT_EQ(compile_commands_json, dbpath);

  tmpdir.SetCwd(ab_rel);
  string foo_path = tmpdir.FullPath("foo.cc");

  std::vector<string> clang_args {
    "clang++",
  };
  string build_dir;
  EXPECT_TRUE(AddCompileOptions(foo_path, dbpath, &clang_args, &build_dir));

  std::vector<string> expected_clang_args {
    "clang++", "-IA", "-IB", "-c", "foo.cc"
  };
  EXPECT_EQ(expected_clang_args, clang_args);
  EXPECT_EQ(ab_abs, build_dir);
}

TEST_F(CompilationDatabaseReaderTest, WithoutCompilationDatabase) {
  std::vector<string> args_after_hyphen_hyphen { "-IA", "-IB" };
  string cwd = "/";
  std::vector<string> extra_arg { "-IC" };
  std::vector<string> extra_arg_before { "-ID" };

  std::vector<string> clang_args { "clang" };
  string build_dir;
  EXPECT_TRUE(MakeClangArgsFromCommandLine(true, args_after_hyphen_hyphen,
                                           "foo.cc", cwd, "", extra_arg,
                                           extra_arg_before,
                                           "", &clang_args, &build_dir));

  std::vector<string> expected_clang_args {
    "clang", "-ID", "-IA", "-IB", "-IC", "-c", "foo.cc"
  };

  EXPECT_EQ(expected_clang_args, clang_args);
  EXPECT_EQ(cwd, build_dir);
}

}  // namespace devtools_goma
