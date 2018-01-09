// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compilation_database_reader.h"

#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <json/json.h>

#include "cmdline_parser.h"
#include "file_helper.h"
#include "flag_parser.h"
#include "path.h"
#include "path_resolver.h"

#ifndef _WIN32
# include <unistd.h>
#else
# include "config_win.h"
# include "posix_helper_win.h"
#endif

using std::string;

namespace devtools_goma {

// From clang-tidy --help, compile_commands.json is searched
// from -p directory (build path). If no build path is specified,
// the directory in the first input file and its all parent paths.
// static
string CompilationDatabaseReader::FindCompilationDatabase(
    StringPiece build_path, StringPiece first_input_file_dir) {
  static const char kCompileCommandsJson[] = "compile_commands.json";

  if (!build_path.empty()) {
    string compdb_path = file::JoinPath(build_path, kCompileCommandsJson);
    if (access(compdb_path.c_str(), R_OK) == 0) {
      return compdb_path;
    }
    return string();
  }

  StringPiece dir = first_input_file_dir;
  while (!dir.empty()) {
    string s = file::JoinPath(dir, kCompileCommandsJson);

    if (access(s.c_str(), R_OK) == 0) {
      return s;
    }

    if (dir == file::Dirname(dir)) {
      break;
    }
    dir = file::Dirname(dir);
  }

  return string();
}

// static
bool CompilationDatabaseReader::MakeClangArgs(
    const ClangTidyFlags& clang_tidy_flags,
    const std::string& compdb_path,
    std::vector<string>* clang_args,
    string* build_dir) {
  // Make clang command from clang-tidy command.
  //
  // If clang command line is specified after '--', we use it.
  // When '--' is not specified, we need to check compile_commands.json.
  //
  // The current command order:
  // With compilation database:
  //   1. options in -extra-arg-before
  //   2. options in compilation database
  //   3. options in -extra-arg
  // Without compilation database:
  //   1. options in -extra-arg-before
  //   2. options after '--'
  //   3. options in -extra-arg
  //   4. -c <input source file>

  if (clang_tidy_flags.input_filenames().size() != 1) {
    LOG(ERROR) << "No input source file or multiple source files. "
               << "size=" << clang_tidy_flags.input_filenames().size();
    return false;
  }

  // -x lang is set later for IncludeProcessor. So, it would be OK to use
  // clang here.
  const std::vector<string>& args = clang_tidy_flags.expanded_args();
  clang_args->push_back(file::JoinPath(file::Dirname(args[0]), "clang"));

  return MakeClangArgsFromCommandLine(
      clang_tidy_flags.seen_hyphen_hyphen(),
      clang_tidy_flags.args_after_hyphen_hyphen(),
      clang_tidy_flags.input_filenames()[0],
      clang_tidy_flags.cwd(),
      clang_tidy_flags.build_path(),
      clang_tidy_flags.extra_arg(),
      clang_tidy_flags.extra_arg_before(),
      compdb_path,
      clang_args,
      build_dir);
}

// static
bool CompilationDatabaseReader::MakeClangArgsFromCommandLine(
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

  // clang_args should have a path to clang only.
  DCHECK_EQ(1U, clang_args->size());

  for (const auto& arg : extra_arg_before) {
    clang_args->push_back(arg);
  }

  if (seen_hyphen_hyphen) {
    // When '--' is seen, compilation database won't be read.
    // In that case, we can consider the current directory is the build dir.

    // Implementation note: args_after_hyphen_hyphen could be still empty.
    // e.g. "clang-tidy foo.cc --"
    // In this case, compilation database should be ignored.

    *build_dir = cwd;
    for (const auto& arg : args_after_hyphen_hyphen) {
      clang_args->push_back(arg);
    }
  } else {
    string source = file::JoinPathRespectAbsolute(cwd, input_file);

    // TODO: Cache the content.
    std::vector<string> new_compile_options;
    bool compdb_successful = AddCompileOptions(
        source, compdb_path, &new_compile_options, build_dir);
    if (!compdb_successful) {
      LOG(ERROR) << "compilation database is corrupted or no entry is found"
                 << " for " << source;
      return false;
    }
    for (const auto& arg : new_compile_options) {
      clang_args->push_back(arg);
    }
  }

  for (const auto& arg : extra_arg) {
    clang_args->push_back(arg);
  }

  if (!args_after_hyphen_hyphen.empty()) {
    clang_args->push_back("-c");
    clang_args->push_back(input_file);
  }

  return true;
}

// static
bool CompilationDatabaseReader::AddCompileOptions(
    const string& source,
    const string& db_path,
    std::vector<string>* clang_args,
    string* build_dir) {
  if (db_path.empty()) {
    // compile_commands.json is not found.
    return false;
  }

  // TODO: Cache the parsed content.
  string content;
  if (!ReadFileToString(db_path, &content)) {
    // couldn't read compile_commands.json
    return false;
  }

  // compile_commands.json should be something like this:
  // [
  //  { "directory": "/home/user/llvm/build",
  //    "command": "/usr/bin/clang++ -Irelative ...",
  //    "file": "file.cc" },
  //  ...
  // ]

  Json::Reader reader;
  Json::Value root;
  if (!reader.parse(content, root, false)) {
    // couldn't parse json in compile_commands.json
    return false;
  }

  if (!root.isArray()) {
    return false;
  }

  string resolved_source = PathResolver::ResolvePath(source);

  string command;
  for (const auto& v : root) {
    if (!v.isMember("directory") || !v["directory"].isString()
        || !v.isMember("command") || !v["command"].isString()
        || !v.isMember("file") || !v["file"].isString()) {
      return false;
    }

    const string db_dir = v["directory"].asString();
    const string db_command = v["command"].asString();
    const string db_file = v["file"].asString();

    string resolved_source_in_db =
        PathResolver::ResolvePath(file::JoinPath(db_dir, db_file));

    if (resolved_source == resolved_source_in_db) {
      // Entry found.
      *build_dir = db_dir;
      command = db_command;
      break;
    }
  }

  if (command.empty()) {
    // corresponding compilation entry is not found.
    return false;
  }

  std::vector<string> argv;
  ParsePosixCommandLineToArgv(command, &argv);

  // When gomacc is used, compilation database might contain gomacc as the first
  // argument. We need to skip it. Also we'd like to skip compiler itself, too.
  // Note: when gomacc is prepended in compilation database command, and goma
  // is not used, clang-tidy looks working well. (Otherwise, we need to change
  // compile_commands.json content before sending goma server.)

  // TODO: Might be better to remove -c and input files?
  // It looks it won't change the result, though...

  size_t init_pos = 1;
  if (!argv.empty()) {
    string argv0 = string(file::Stem(argv[0]));
    std::transform(argv0.begin(), argv0.end(), argv0.begin(), ::tolower);
    if (argv0 == "gomacc") {
      init_pos = 2;
    }
  }
  for (size_t i = init_pos; i < argv.size(); ++i) {
    clang_args->push_back(argv[i]);
  }

  return true;
}

}  // namespace devtools_goma
