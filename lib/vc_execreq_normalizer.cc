// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/vc_execreq_normalizer.h"

#include "glog/logging.h"
#include "lib/clang_flags_helper.h"
#include "lib/vc_flags.h"
using std::string;

namespace devtools_goma {

ConfigurableExecReqNormalizer::Config VCExecReqNormalizer::Configure(
    int id,
    const std::vector<string>& args,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    const ExecReq* req) const {
  int keep_cwd = kOmit;
  int keep_args = kNormalizeWithCwd;
  int keep_pathnames_in_input = kNormalizeWithCwd;
  int keep_system_include_dirs = kNormalizeWithCwd;

  if (normalize_weak_relative_for_arg.empty()) {
    keep_args |= kAsIs;
  }
  if (!normalize_include_path) {
    keep_system_include_dirs |= kAsIs;
  }

  // TODO: check what is good for linking.
  if (is_linking) {
    // We preserve anything for linking but we may omit file contents.
    keep_cwd |= kAsIs;
    keep_args |= kAsIs;
    keep_pathnames_in_input |= kAsIs;
    keep_system_include_dirs |= kAsIs;
  }

  bool is_clang_cl = VCFlags::IsClangClCommand(req->command_spec().name());
  FlagParser flag_parser;
  VCFlags::DefineFlags(&flag_parser);
  FlagParser::Flag* flag_z7 = flag_parser.AddBoolFlag("Z7");
  FlagParser::Flag* flag_zi = flag_parser.AddBoolFlag("Zi");
  FlagParser::Flag* flag_zI = flag_parser.AddBoolFlag("ZI");
  FlagParser::Flag* flag_fprofile_instr_generate =
      flag_parser.AddBoolFlag("fprofile-instr-generate");
  FlagParser::Flag* flag_fcoverage_mapping =
      flag_parser.AddBoolFlag("fcoverage-mapping");
  FlagParser::Flag* flag_fc = flag_parser.AddBoolFlag("FC");
  FlagParser::Flag* flag_fdiagnostics_absolute_paths =
      flag_parser.AddBoolFlag("fdiagnostics-absolute-paths");
  FlagParser::Flag* flag_show_include = flag_parser.AddBoolFlag("showIncludes");
  flag_parser.Parse(args);

  if (flag_show_include->seen()) {
    // /showInclude outputs path as-is. So we need to preserve input path.
    keep_pathnames_in_input |= kAsIs;
  }

  if (flag_fc->seen() || flag_fdiagnostics_absolute_paths->seen()) {
    // With this option, full input path would be shown in
    // stdout (/showIncludes) or stderr (compile error).  We must preserve cwd
    // paths.
    // Currently (2018/06/19), clang-cl ignores /FC option, but it may change
    // behavior. So keep cwd with /FC in clang-cl case too.
    // This flag has higher priority than -fdebug-compilation-dir.
    keep_cwd |= kAsIs;
  }

  absl::optional<string> fdebug_compilation_dir;
  if (is_clang_cl && !(keep_cwd & kAsIs)) {
    ClangFlagsHelper clang_flags_helper(args);
    fdebug_compilation_dir = clang_flags_helper.fdebug_compilation_dir();
  }

  if (flag_z7->seen() || flag_zi->seen() || flag_zI->seen()) {
    // If debug info option is set, we must keep args, pathnames,
    // system include dirs as-is.
    // But we can replace cwd when fdebug-compilation-dir is set.
    if (!fdebug_compilation_dir) {
      keep_cwd |= kAsIs;
    }
    keep_args |= kAsIs;
    keep_pathnames_in_input |= kAsIs;
    keep_system_include_dirs |= kAsIs;
  }
  if (is_clang_cl && flag_fprofile_instr_generate->seen() &&
      flag_fcoverage_mapping->seen()) {
    keep_cwd |= kAsIs;
    keep_pathnames_in_input |= kAsIs;
  }

  // TODO: Currently the logic of keep_args is assuming args can be
  // parsed with GCCFlags. Parsing command line for cl.exe (or clang-cl.exe)
  // with GCCFlags is always wrong.
  //
  // Don't normalize args for cl.exe and clang-cl.exe until the code
  // has fixed. Fortunately, absolute path won't appear in chrome build.
  // So, the result of normalize won't change.
  keep_args |= kAsIs;

  Config config;
  config.keep_cwd = keep_cwd;
  config.keep_args = keep_args;
  config.keep_pathnames_in_input = keep_pathnames_in_input;
  config.keep_system_include_dirs = keep_system_include_dirs;
  config.new_cwd = std::move(fdebug_compilation_dir);

  // Dropping pathnames can generate same hash from different input.
  CHECK(!(config.keep_pathnames_in_input & kOmit));
  return config;
}

}  // namespace devtools_goma
