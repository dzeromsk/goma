// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compiler_flag_utils.h"

#include "clang_tidy_flags.h"
#include "compilation_database_reader.h"
#include "glog/logging.h"
#include "path.h"

namespace devtools_goma {

void InitClangTidyFlags(ClangTidyFlags* flags) {
  if (flags->input_filenames().size() != 1) {
    flags->Fail("Input file is not unique.");
    return;
  }
  const string& input_file = flags->input_filenames()[0];
  const string input_file_abs =
      file::JoinPathRespectAbsolute(flags->cwd(), input_file);
  string compdb_path = CompilationDatabaseReader::FindCompilationDatabase(
      flags->build_path(), file::Dirname(input_file_abs));

  std::vector<string> clang_args;
  string build_dir;
  if (!CompilationDatabaseReader::MakeClangArgs(*flags, compdb_path,
                                                &clang_args, &build_dir)) {
    // Failed to make clang args. Then Mark CompilerFlags unsuccessful.
    flags->Fail("Failed to make clang args. local fallback.");
    return;
  }

  DCHECK(!build_dir.empty());
  flags->SetCompilationDatabasePath(compdb_path);
  flags->SetClangArgs(clang_args, build_dir);
}

}  // namespace devtools_goma
