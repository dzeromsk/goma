// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "gcc_execreq_normalizer.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_join.h"
#include "clang_flags_helper.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
using std::string;

namespace devtools_goma {

ConfigurableExecReqNormalizer::Config GCCExecReqNormalizer::Configure(
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

  bool is_clang = GCCFlags::IsClangCommand(req->command_spec().name());
  FlagParser flag_parser;
  GCCFlags::DefineFlags(&flag_parser);
  FlagParser::Flag* flag_g = flag_parser.AddPrefixFlag("g");
  FlagParser::Flag* flag_gsplit_dwarf = flag_parser.AddBoolFlag("gsplit-dwarf");
  FlagParser::Flag* flag_m = flag_parser.AddBoolFlag("M");
  FlagParser::Flag* flag_md = flag_parser.AddBoolFlag("MD");
  FlagParser::Flag* flag_mmd = flag_parser.AddBoolFlag("MMD");
  FlagParser::Flag* flag_pnacl_allow_translate =
      flag_parser.AddBoolFlag("-pnacl-allow-translate");
  FlagParser::Flag* flag_fprofile_instr_generate =
      flag_parser.AddBoolFlag("fprofile-instr-generate");
  FlagParser::Flag* flag_fcoverage_mapping =
      flag_parser.AddBoolFlag("fcoverage-mapping");
  flag_parser.Parse(args);

  // -g does not capture -gsplit-dwarf. So we need to check it explicitly.
  bool has_debug_flag = false;
  if ((flag_g->seen() && flag_g->GetLastValue() != "0") ||
      flag_gsplit_dwarf->seen()) {
    // The last -g* is effective.
    // If the last one is -g0, it is not debug build.
    has_debug_flag = true;
  }

  absl::optional<string> fdebug_compilation_dir;
  if (has_debug_flag) {
    ClangFlagsHelper clang_flags_helper(args);
    fdebug_compilation_dir = clang_flags_helper.fdebug_compilation_dir();
  }

  bool has_m_flag = false;
  if (flag_m->seen() || (flag_md->seen() && is_clang) ||
      (flag_md->seen() && !flag_mmd->seen())) {
    // We basically need to preserve all include paths if we see -M, -MD.
    // With -M and -MD, full path input files are stored in .d file.
    //
    // Note that -MMD works opposite between clang and gcc.
    // clang ignores -MMD if it is used with -M or -MD.
    // gcc ignores -MD or -M if -MMD is specified.
    has_m_flag = true;
  }

  // TODO: support relative path rewrite using debug-prefix-map.
  // -fdebug-prefix-map=foo=bar is valid but it makes path conversion
  // difficult to predict.
  //
  // TODO: support cross compile.
  // I belive this feature will be used for cross compiling Windows code on
  // Linux.  e.g. converting /home/foo to c:\\Users\\Foo.
  //
  // Although, clang-cl does not know -fdebug-prefix-map, it works with
  // -Xclang
  // $ clang-cl -Xclang -fdebug-prefix-map=/tmp=c:\\foo /Zi /c /tmp/foo.c
  // and its debug info has c:\foo\foo.c.
  if (has_debug_flag) {
    // For debug build, we should keep cwd, system include paths,
    // paths in input files.  However, all of them could be normalized
    // with debug prefix map.
    // (Note that if this is used with -M or -MD, restrictions for
    // -M or -MD would be prioritized.
    bool has_ambiguity = HasAmbiguityInDebugPrefixMap(debug_prefix_map);
    LOG_IF(ERROR, has_ambiguity)
        << id << ": has ambiguity in -fdebug_prefix_map. "
        << "goma server won't normalize ExecReq."
        << " debug_prefix_map=" << debug_prefix_map;
    if (!has_ambiguity && !debug_prefix_map.empty()) {
      keep_cwd |= kNormalizeWithDebugPrefixMap;
      keep_system_include_dirs |= kNormalizeWithDebugPrefixMap;
      keep_pathnames_in_input |= kNormalizeWithDebugPrefixMap;
      if (is_clang) {
        keep_args |= kNormalizeWithDebugPrefixMap;
      } else {
        // gcc has command line in DW_AT_producer but clang does not.
        keep_args |= kAsIs;
      }
    } else {
      // If we do not use fdebug-compilation-dir, we need to keep cwd.
      if (!fdebug_compilation_dir) {
        keep_cwd |= kAsIs;
      }
      keep_system_include_dirs |= kAsIs;
      keep_pathnames_in_input |= kAsIs;
      keep_args |= kAsIs;
    }
  }

  if (has_m_flag) {
    keep_system_include_dirs |= kAsIs;
    keep_args |= kPreserveI;
  }
  if (flag_pnacl_allow_translate->seen()) {
    // Absolute source file path name would be set in symtab if pnacl-clang
    // translate output to ELF.  See: crbug.com/685461
    keep_cwd |= kAsIs;
  }
  if (is_clang && flag_fprofile_instr_generate->seen() &&
      flag_fcoverage_mapping->seen()) {
    keep_cwd |= kAsIs;
    keep_pathnames_in_input |= kAsIs;
  }

  // TODO: check what is good for linking.
  if (is_linking) {
    // We preserve anything for linking but we may omit file contents.
    keep_cwd |= kAsIs;
    keep_args |= kAsIs;
    keep_pathnames_in_input |= kAsIs;
    keep_system_include_dirs |= kAsIs;
  }

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

void GCCExecReqNormalizer::NormalizeExecReqArgs(
    int keep_args,
    const std::vector<string>& args,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    const string& debug_prefix_map_signature,
    ExecReq* req) const {
  if (keep_args & kAsIs) {
    return;
  }

  // Normalize arguments after certain flags.
  // This is required for
  // - libFindBadConstructs.so plugin used in chrome clang. b/9957696.
  // - -B to choose third_party/binutils used in chrome. b/13940741.
  // - -gcc-toolchain= for clang to find headers. b/16876457.
  FlagParser parser;
  GCCFlags::DefineFlags(&parser);

  std::unique_ptr<PathRewriterWithDebugPrefixMap> rewrite_path;
  // Use this to remove -fdebug-prefix-map in Release build b/28280739
  if (keep_args & kNormalizeWithDebugPrefixMap) {
    rewrite_path =
        absl::make_unique<PathRewriterWithDebugPrefixMap>(debug_prefix_map);
  } else {
    rewrite_path = absl::make_unique<PathRewriterWithDebugPrefixMap>(
        std::map<string, string>());
  }
  parser.AddFlag("fdebug-prefix-map")
      ->SetCallbackForParsedArgs(rewrite_path.get());

  FixPathToBeCwdRelative fix_path(req->cwd());
  for (const auto& flag : normalize_weak_relative_for_arg) {
    if ((keep_args & kPreserveI) && (flag == "I" || flag == "isystem")) {
      continue;
    }
    if (keep_args & kNormalizeWithDebugPrefixMap) {
      parser.AddFlag(flag.c_str())
          ->SetCallbackForParsedArgs(rewrite_path.get());
    } else if (keep_args & kNormalizeWithCwd) {
      parser.AddFlag(flag.c_str())->SetCallbackForParsedArgs(&fix_path);
    }
  }

  parser.Parse(args);
  if (fix_path.is_fixed() || rewrite_path->removed_fdebug_prefix_map()) {
    std::vector<string> parsed_args = parser.GetParsedArgs();
    if (req->expanded_arg_size() > 0) {
      req->clear_expanded_arg();
      std::copy(parsed_args.begin(), parsed_args.end(),
                RepeatedFieldBackInserter(req->mutable_expanded_arg()));
    } else {
      req->clear_arg();
      std::copy(parsed_args.begin(), parsed_args.end(),
                RepeatedFieldBackInserter(req->mutable_arg()));
    }

    CommandSpec* normalized_spec = req->mutable_command_spec();
    if (fix_path.is_fixed()) {
      normalized_spec->mutable_comment()->append(
          " args:cwd:" + absl::StrJoin(normalize_weak_relative_for_arg, ","));
    }
    if (rewrite_path->removed_fdebug_prefix_map()) {
      normalized_spec->mutable_comment()->append(
          " args:removed_-fdebug-prefix-map");
    }
    if (rewrite_path->is_rewritten()) {
      normalized_spec->mutable_comment()->append(" args:" +
                                                 debug_prefix_map_signature);
    }
  }
}

}  // namespace devtools_goma
