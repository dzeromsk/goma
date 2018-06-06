// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COMPILATION_DATABASE_READER_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILATION_DATABASE_READER_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "clang_tidy_flags.h"

namespace devtools_goma {

// The implementation to read a compilation database (compile_commands.json).
class CompilationDatabaseReader {
 public:
  CompilationDatabaseReader() = delete;
  CompilationDatabaseReader(const CompilationDatabaseReader&) = delete;

  CompilationDatabaseReader& operator=(
      const CompilationDatabaseReader&) = delete;

  // Finds compile_commands.json in |build_path|, or |dir| and its ancestors.
  // The ancestors of |build_path| won't be searched.
  //
  // Returns the path to compile_commands.json.
  // Return empty string if not found.
  static std::string FindCompilationDatabase(
      absl::string_view build_path, absl::string_view dir);

  // Creates corresponding clang args from clang tidy flag for IncludeProcessor.
  static bool MakeClangArgs(const ClangTidyFlags& clang_tidy_flags,
                            const std::string& compdb_path,
                            std::vector<std::string>* clang_args,
                            std::string* build_dir);

 private:
  // Parses a compilation database at |db_path|, and add options to
  // |clang_args|.
  // Returns true if succeeded. |build_dir| will contain the directory in
  // the compilation database entry.
  // Returns false if parsing compilation database is failed or
  // compilation entry for |source| is not found in the compilation database.
  static bool AddCompileOptions(const std::string& source,
                                const std::string& db_path,
                                std::vector<std::string>* clang_args,
                                std::string* build_dir);

  // MakeClangArgs that does not depend on ClangTidyFlags.
  // Note: When command line is "clang-tidy foo.cc --", compilation database
  // should be ignored.
  static bool MakeClangArgsFromCommandLine(
      bool seen_hyphen_hyphen,
      const std::vector<std::string>& args_after_hyphen_hyphen,
      const string& input_file,
      const std::string& cwd,
      const std::string& build_path,
      const std::vector<std::string>& extra_arg,
      const std::vector<std::string>& extra_arg_before,
      const std::string& compdb_path,
      std::vector<std::string>* clang_args,
      std::string* build_dir);

  friend class CompilationDatabaseReaderTest;
};

} // namespace devtools_goma

#endif // DEVTOOLS_GOMA_CLIENT_COMPILATION_DATABASE_READER_H_
