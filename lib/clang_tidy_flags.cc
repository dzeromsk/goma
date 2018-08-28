// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "clang_tidy_flags.h"

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "gcc_flags.h"
#include "path.h"
#include "path_util.h"

namespace devtools_goma {

ClangTidyFlags::ClangTidyFlags(const std::vector<string>& args,
                               const string& cwd)
    : CxxFlags(args, cwd), seen_hyphen_hyphen_(false) {
  if (!CompilerFlags::ExpandPosixArgs(cwd, args, &expanded_args_,
                                      &optional_input_filenames_)) {
    Fail("Unable to expand args", args);
    return;
  }

  FlagParser parser;
  DefineFlags(&parser);

  FlagParser::Flag* flag_export_fixes = parser.AddFlag("export-fixes");
  parser.AddFlag("extra-arg")->SetValueOutputWithCallback(nullptr, &extra_arg_);
  parser.AddFlag("extra-arg-before")
      ->SetValueOutputWithCallback(nullptr, &extra_arg_before_);
  FlagParser::Flag* flag_p = parser.AddFlag("p");

  parser.Parse(expanded_args_);
  unknown_flags_ = parser.unknown_flag_args();

  if (flag_p->seen()) {
    build_path_ = flag_p->GetLastValue();
  }

  // The file specified in -export-fix will have suggested fix.
  // This can be considered as output.
  if (flag_export_fixes->seen()) {
    output_files_.push_back(flag_export_fixes->GetLastValue());
  }

  // We use absolute path for source_files.
  // clang-tidy has 2 kinds of current working directory.
  // One is for clang-tidy itself, the other is for include processor,
  // which is specified in the compilation database.
  // Converting them is hard, so we'd like to use absolute path.
  std::vector<string> source_files;
  for (size_t i = 1; i < args.size(); ++i) {
    if (seen_hyphen_hyphen_) {
      args_after_hyphen_hyphen_.push_back(args[i]);
      continue;
    }

    if (args[i] == "--") {
      seen_hyphen_hyphen_ = true;
      continue;
    }

    if (!args[i].empty() && args[i][0] == '-') {
      // Skip this option since this is clang-tidy option.
      continue;
    }
    source_files.push_back(file::JoinPath(cwd, args[i]));
  }

  input_filenames_ = std::move(source_files);
  is_successful_ = true;
}

const string& ClangTidyFlags::cwd_for_include_processor() const {
  return gcc_flags_->cwd();
}

void ClangTidyFlags::SetClangArgs(const std::vector<string>& clang_args,
                                  const string& dir) {
  gcc_flags_ = absl::make_unique<GCCFlags>(clang_args, dir);
  is_successful_ = is_successful_ && gcc_flags_->is_successful();
  lang_ = gcc_flags_->lang();
}

void ClangTidyFlags::SetCompilationDatabasePath(const string& compdb_path) {
  optional_input_filenames_.push_back(compdb_path);
}

const std::vector<string>& ClangTidyFlags::non_system_include_dirs() const {
  return gcc_flags_->non_system_include_dirs();
}

const std::vector<string>& ClangTidyFlags::root_includes() const {
  return gcc_flags_->root_includes();
}

const std::vector<string>& ClangTidyFlags::framework_dirs() const {
  return gcc_flags_->framework_dirs();
}

const std::vector<std::pair<string, bool>>& ClangTidyFlags::commandline_macros()
    const {
  return gcc_flags_->commandline_macros();
}

bool ClangTidyFlags::is_cplusplus() const {
  return gcc_flags_->is_cplusplus();
}
bool ClangTidyFlags::has_nostdinc() const {
  return gcc_flags_->has_nostdinc();
}

string ClangTidyFlags::compiler_name() const {
  return "clang-tidy";
}

// static
void ClangTidyFlags::DefineFlags(FlagParser* parser) {
  FlagParser::Options* opts = parser->mutable_options();
  opts->flag_prefix = '-';
  opts->allows_equal_arg = true;
  opts->allows_nonspace_arg = true;

  parser->AddBoolFlag("analyze-temporary-dtors");
  parser->AddFlag("checks");
  parser->AddFlag("config");
  parser->AddBoolFlag("dump_config");
  parser->AddBoolFlag("enable-check-profile");
  parser->AddBoolFlag("explain-config");
  parser->AddBoolFlag("fix");
  parser->AddBoolFlag("fix-errors");
  parser->AddFlag("header-filter");
  parser->AddFlag("line-filter");
  parser->AddFlag("p");
  parser->AddBoolFlag("list-checks");
  parser->AddBoolFlag("system-headers");
  parser->AddBoolFlag("warning-as-errors");
}

/* static */
bool ClangTidyFlags::IsClangTidyCommand(absl::string_view arg) {
  return absl::AsciiStrToLower(GetBasename(arg)) == "clang-tidy";
}

// static
string ClangTidyFlags::GetCompilerName(absl::string_view /*arg*/) {
  return "clang-tidy";
}

}  // namespace devtools_goma
