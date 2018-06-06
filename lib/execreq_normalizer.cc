// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "execreq_normalizer.h"

#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "compiler_flags.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "java_flags.h"
#include "path.h"
#include "path_resolver.h"
#include "path_util.h"
#include "vc_flags.h"
using ::google::protobuf::RepeatedPtrField;
using ::absl::StrCat;

namespace devtools_goma {

namespace {

class FixPath : public FlagParser::Callback {
 public:
  explicit FixPath(string cwd) : cwd_(std::move(cwd)), is_fixed_(false) {}
  string ParseFlagValue(const FlagParser::Flag& flag,
                        const string& value) override {
    string normalized_path = PathResolver::WeakRelativePath(value, cwd_);
    if (normalized_path != value) {
      is_fixed_ = true;
    }
    return normalized_path;
  }

  bool is_fixed() const { return is_fixed_; }

 private:
  const string cwd_;
  bool is_fixed_;
};

class RewritePath : public FlagParser::Callback {
 public:
  explicit RewritePath(const std::map<string, string>& debug_prefix_map)
      : debug_prefix_map_(debug_prefix_map),
        is_rewritten_(false), removed_fdebug_prefix_map_(false) {}

  string ParseFlagValue(const FlagParser::Flag& flag,
                        const string& value) override {
    // TODO: need to support Windows?
    if (!IsPosixAbsolutePath(value)) {
      return value;
    }
    // RewritePath is used for normalizing paths.
    // We MUST eliminate anything in debug-prefix-map.
    if (flag.name() == "fdebug-prefix-map") {
      removed_fdebug_prefix_map_ = true;
      return "";
    }

    string path = value;
    if (RewritePathWithDebugPrefixMap(debug_prefix_map_, &path)) {
      is_rewritten_ = true;
      return path;
    }
    return value;
  }

  bool is_rewritten() const { return is_rewritten_; }
  bool removed_fdebug_prefix_map() const { return removed_fdebug_prefix_map_; }

 private:
  const std::map<string, string>& debug_prefix_map_;
  bool is_rewritten_;
  bool removed_fdebug_prefix_map_;
};

}  // anonymous namespace

bool RewritePathWithDebugPrefixMap(
    const std::map<string, string>& debug_prefix_map,
    string* path) {
  if (path->empty()) {
    return false;
  }

  // See CGDebugInfo::remapDIPath
  // https://clang.llvm.org/doxygen/CGDebugInfo_8cpp_source.html
  for (const auto& iter : debug_prefix_map) {
    if (absl::StartsWith(*path, iter.first)) {
      *path = file::JoinPath(iter.second, path->substr(iter.first.length()));
      return true;
    }
  }
  return false;
}

// We say debug prefix map is ambiguous when the application order of debug
// prefix map can change the final result.
// For example:
//   Suppose we have the following debug prefix maps:
//     /A = /X    (1)
//     /A/B = /Y  (2)
//   and we want to rewrite /A/B/C.
//   /A/B/C is written to /X/B/C with (1), but is also written to /Y/C with (2).
// When such a case happens, we say debug prefix map is ambiguous.
//
// In clang and gcc, only first matched rule is used to rewrite path.
// https://clang.llvm.org/doxygen/CGDebugInfo_8cpp_source.html
// (CGDebugInfo::remapDIPath)
// https://github.com/gcc-mirror/gcc/blob/460902cc8ac206904e7f1763f197927be87b122f/gcc/final.c#L1562
//
// TODO: If the application order of debug_prefix_map is written-order,
// using std::vector<std::pair<string, string>> looks better than
// std::map<string, string>?
bool HasAmbiguityInDebugPrefixMap(
    const std::map<string, string>& debug_prefix_map) {
  if (debug_prefix_map.size() <= 1) {
    return false;
  }

  string prev;
  for (const auto& path : debug_prefix_map) {
    if (!prev.empty() && absl::StartsWith(path.first, prev)) {
      return true;
    }
    prev = path.first;
  }
  return false;
}

void ConfigurableExecReqNormalizer::NormalizeExecReqSystemIncludeDirs(
    int keep_system_include_dirs,
    const std::map<string, string>& debug_prefix_map,
    const string& debug_prefix_map_signature,
    ExecReq* req) const {
  if (keep_system_include_dirs & kAsIs) {
    return;
  }

  // Hack for non-system-default compilers e.g. NaCl and clang.
  // Normalize following paths to be given with the relative path:
  // - system_include_path
  // - cxx_system_include_path
  //
  // Already cleared:
  // - local_compiler_path
  //
  // Note:
  // Since followings are usually pointing the system default paths,
  // we do not normalize them.
  // - system_framework_path
  // - system_library_path
  CommandSpec* normalized_spec = req->mutable_command_spec();
  // To avoid yet another cache poisoning, we should separate cache area.
  // i.e. include_paths with relative paths is given but misunderstand
  // it as not normalized.
  if (keep_system_include_dirs & kNormalizeWithDebugPrefixMap) {
    bool is_normalized = false;
    for (auto& path : *normalized_spec->mutable_system_include_path()) {
      is_normalized |= RewritePathWithDebugPrefixMap(debug_prefix_map, &path);
    }
    for (auto& path : *normalized_spec->mutable_cxx_system_include_path()) {
      is_normalized |= RewritePathWithDebugPrefixMap(debug_prefix_map, &path);
    }
    if (is_normalized) {
      normalized_spec->mutable_comment()->append(" include_path:" +
                                                 debug_prefix_map_signature);
    }
  } else if (keep_system_include_dirs & kNormalizeWithCwd) {
    bool is_include_path_normalized = false;
    for (auto& path : *normalized_spec->mutable_system_include_path()) {
      string normalized_path = PathResolver::WeakRelativePath(path, req->cwd());
      if (path != normalized_path) {
        path.assign(normalized_path);
        is_include_path_normalized = true;
      }
    }
    for (auto& path : *normalized_spec->mutable_cxx_system_include_path()) {
      string normalized_path = PathResolver::WeakRelativePath(path, req->cwd());
      if (path != normalized_path) {
        path.assign(normalized_path);
        is_include_path_normalized = true;
      }
    }
    if (is_include_path_normalized) {
      normalized_spec->mutable_comment()->append(" include_path:cwd");
    }
  } else if (keep_system_include_dirs == kOmit) {
    normalized_spec->clear_system_include_path();
    normalized_spec->clear_cxx_system_include_path();
    normalized_spec->mutable_comment()->append(" omit_include_path:");
  } else {
    DLOG(FATAL) << "Unexpected keep_system_include_dirs="
                << keep_system_include_dirs;
  }
}

void ConfigurableExecReqNormalizer::NormalizeExecReqArgs(
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

  std::unique_ptr<RewritePath> rewrite_path;
  // Use this to remove -fdebug-prefix-map in Release build b/28280739
  if (keep_args & kNormalizeWithDebugPrefixMap) {
    rewrite_path = absl::make_unique<RewritePath>(debug_prefix_map);
  } else {
    rewrite_path = absl::make_unique<RewritePath>((std::map<string, string>()));
  }
  parser.AddFlag("fdebug-prefix-map")
      ->SetCallbackForParsedArgs(rewrite_path.get());

  FixPath fix_path(req->cwd());
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

void ConfigurableExecReqNormalizer::NormalizeExecReqPathnamesInInput(
    int keep_pathnames_in_input,
    const std::map<string, string>& debug_prefix_map,
    const string& debug_prefix_map_signature,
    ExecReq* req) const {
  if (keep_pathnames_in_input & kAsIs) {
    return;
  }

  bool is_rewritten_debug_prefix_map = false;
  bool is_rewritten_cwd = false;
  bool is_removed = false;
  for (auto& input : *req->mutable_input()) {
    if (keep_pathnames_in_input & kNormalizeWithDebugPrefixMap) {
      RewritePathWithDebugPrefixMap(debug_prefix_map, input.mutable_filename());
      is_rewritten_debug_prefix_map = true;
    } else if (keep_pathnames_in_input & kNormalizeWithCwd) {
      input.set_filename(
          PathResolver::WeakRelativePath(input.filename(), req->cwd()));
      is_rewritten_cwd = true;
    } else if (keep_pathnames_in_input == kOmit) {
      input.clear_filename();
      is_removed = true;
    } else {
      DLOG(FATAL) << "Unexpected keep_pathnames_in_input="
                  << keep_pathnames_in_input;
    }
  }

  CommandSpec* normalized_spec = req->mutable_command_spec();
  if (is_rewritten_debug_prefix_map) {
    normalized_spec->mutable_comment()->append(" pathnames_in_input:" +
                                               debug_prefix_map_signature);
  }
  if (is_rewritten_cwd) {
    normalized_spec->mutable_comment()->append(" pathnames_in_input:cwd");
  }
  if (is_removed) {
    normalized_spec->mutable_comment()->append(" pathnames_in_input:removed");
  }
}

void ConfigurableExecReqNormalizer::NormalizeExecReqCwd(
    int keep_cwd,
    const std::map<string, string>& debug_prefix_map,
    const string& debug_prefix_map_signature,
    ExecReq* req) const {
  if (keep_cwd & kAsIs) {
    return;
  }

  bool is_rewritten = false;
  bool is_removed = false;

  static const char kPwd[] = "PWD=";

  if (keep_cwd & kNormalizeWithDebugPrefixMap) {
    // If there is PWD= in env, replace cwd with content of PWD=.
    for (const auto& env_var : req->env()) {
      if (absl::StartsWith(env_var, kPwd)) {
        *req->mutable_cwd() = env_var.substr(strlen(kPwd));
        break;
      }
    }
    RewritePathWithDebugPrefixMap(debug_prefix_map, req->mutable_cwd());
    is_rewritten = true;
  } else {
    req->clear_cwd();
    is_removed = true;
  }

  // Drop PWD from env.
  auto it = req->mutable_env()->begin();
  while (it != req->mutable_env()->end()) {
    if (absl::StartsWith(*it, kPwd)) {
      if (keep_cwd & kNormalizeWithDebugPrefixMap) {
        string path = it->substr(strlen(kPwd));
        RewritePathWithDebugPrefixMap(debug_prefix_map, &path);
        *it = StrCat(kPwd, path);
        is_rewritten = true;
        ++it;
      } else {
        it = req->mutable_env()->erase(it);
        is_removed = true;
      }
    } else {
      ++it;
    }
  }

  CommandSpec* normalized_spec = req->mutable_command_spec();
  if (is_rewritten) {
    normalized_spec->mutable_comment()->append(" cwd:" +
                                               debug_prefix_map_signature);
  }
  if (is_removed) {
    normalized_spec->mutable_comment()->append(" cwd:removed");
  }
}

void ConfigurableExecReqNormalizer::NormalizeExecReqSubprograms(
    ExecReq* req) const {
  // normalize subprogram. path names are not needed for cache key.
  for (auto& s : *req->mutable_subprogram()) {
    s.clear_path();
  }
}

void ConfigurableExecReqNormalizer::NormalizeExecReqEnvs(ExecReq* req) const {
  std::vector<string> new_env;
  bool changed = false;
  for (const auto& env_var : req->env()) {
    if (absl::StartsWith(env_var, "DEVELOPER_DIR=")) {
      changed = true;
      continue;
    }
    new_env.push_back(env_var);
  }
  if (changed) {
    req->clear_env();
    for (auto&& env_var : new_env) {
      req->add_env(std::move(env_var));
    }
  }
}

// ExecReq_Inputs are sorted by filename now. However, cwd can be different
// among computers, and filename might contain cwd. So the essentially same
// ExecReq might have different hash values, even if cwd in ExecReq and
// filenames in ExecReq_Input are cleared.
// So we reorder ExecReq_Inputs so that ExecReq_Input whose filename starts with
// cwd come first.
//
// For example: When cwd = /usr/local/google/home/foo/build,
//   the following ExecReq_Inputs
//     ExecReq_Input { filename: /usr/include/stdio.h, ... }
//     ...
//     ...
//     ExecReq_Input { filename: /usr/local/google/home/foo/build/main.cc, ...}
//   will be reorderd to
//     ExecReq_Input { filename: /usr/local/google/home/foo/build/main.cc, ...}
//     ExecReq_Input { filename: /usr/include/stdio.h, ... }
//     ...
//     ...
//
// See also b/11455957
void ConfigurableExecReqNormalizer::NormalizeExecReqInputOrderForCacheKey(
    ExecReq* req) const {
  std::vector<const ExecReq_Input*> inputs_in_cwd;
  std::vector<const ExecReq_Input*> inputs_not_in_cwd;

  inputs_in_cwd.reserve(req->input_size());
  inputs_not_in_cwd.reserve(req->input_size());

  for (const auto& input : req->input()) {
    if (absl::StartsWith(input.filename(), req->cwd())) {
      inputs_in_cwd.push_back(&input);
    } else {
      inputs_not_in_cwd.push_back(&input);
    }
  }

  RepeatedPtrField<ExecReq_Input> new_inputs;
  new_inputs.Reserve(req->input_size());

  // Inputs whose filename starting with cwd come first.
  for (const auto& input : inputs_in_cwd) {
    *new_inputs.Add() = *input;
  }
  for (const auto& input : inputs_not_in_cwd) {
    *new_inputs.Add() = *input;
  }

  DCHECK_EQ(new_inputs.size(), req->input_size());

  req->mutable_input()->Swap(&new_inputs);
}

void NormalizeExecReqForCacheKey(
    const int id,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    ExecReq* req) {
  req->clear_requester_info();
  req->clear_cache_policy();
  req->clear_requester_env();

  for (auto& input : *req->mutable_input()) {
    input.clear_content();
  }

  req->mutable_command_spec()->clear_local_compiler_path();
  const string& command_name = req->command_spec().name();
  LOG_IF(ERROR, command_name.empty())
      << "empty command_spec.name:" << req->command_spec().DebugString();
  std::vector<string> args;
  // Normalize args.
  // we use CommandSpec.name for arg(0) for cache key.
  // see b/11973647
  if (req->expanded_arg_size() > 0) {
    req->set_expanded_arg(0, command_name);
    req->clear_arg();
    std::copy(req->expanded_arg().begin(), req->expanded_arg().end(),
              back_inserter(args));
  } else if (req->arg_size() > 0) {
    req->set_arg(0, command_name);
    std::copy(req->arg().begin(), req->arg().end(), back_inserter(args));
  }

  ConfigurableExecReqNormalizer normalizer;
  normalizer.Normalize(id, args, normalize_include_path, is_linking,
                       normalize_weak_relative_for_arg, debug_prefix_map, req);
}

ConfigurableExecReqNormalizer::Config ConfigurableExecReqNormalizer::Configure(
    int id,
    const std::vector<string>& args,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    const ExecReq* req) const {
  int keep_cwd = kOmit;
  int keep_args = kNormalizeWithCwd;
  int keep_pathnames_in_input = kOmit;
  int keep_system_include_dirs = kNormalizeWithCwd;

  if (normalize_weak_relative_for_arg.empty()) {
    keep_args |= kAsIs;
  }
  if (!normalize_include_path) {
    keep_system_include_dirs |= kAsIs;
  }

  if (GCCFlags::IsGCCCommand(req->command_spec().name())) {
    bool is_clang = GCCFlags::IsClangCommand(req->command_spec().name());
    FlagParser flag_parser;
    GCCFlags::DefineFlags(&flag_parser);
    FlagParser::Flag* flag_g = flag_parser.AddPrefixFlag("g");
    FlagParser::Flag* flag_gsplit_dwarf =
        flag_parser.AddBoolFlag("gsplit-dwarf");
    FlagParser::Flag* flag_m = flag_parser.AddBoolFlag("M");
    FlagParser::Flag* flag_md = flag_parser.AddBoolFlag("MD");
    FlagParser::Flag* flag_mmd = flag_parser.AddBoolFlag("MMD");
    FlagParser::Flag* flag_pnacl_allow_translate = flag_parser.AddBoolFlag(
        "-pnacl-allow-translate");
    FlagParser::Flag* flag_fprofile_instr_generate = flag_parser.AddBoolFlag(
        "fprofile-instr-generate");
    FlagParser::Flag* flag_fcoverage_mapping = flag_parser.AddBoolFlag(
        "fcoverage-mapping");
    flag_parser.Parse(args);

    // -g does not capture -gsplit-dwarf. So we need to check it explicitly.
    bool has_debug_flag = false;
    if ((flag_g->seen() && flag_g->GetLastValue() != "0") ||
        flag_gsplit_dwarf->seen()) {
      // The last -g* is effective.
      // If the last one is -g0, it is not debug build.
      has_debug_flag = true;
    }

    bool has_m_flag = false;
    if (flag_m->seen() ||
        (flag_md->seen() && is_clang) ||
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
        keep_cwd |= kAsIs;
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
    if (is_clang
        && flag_fprofile_instr_generate->seen()
        && flag_fcoverage_mapping->seen()) {
      keep_cwd |= kAsIs;
      keep_pathnames_in_input |= kAsIs;
    }
  } else if (VCFlags::IsVCCommand(req->command_spec().name())) {
    bool is_clang_cl = VCFlags::IsClangClCommand(req->command_spec().name());
    FlagParser flag_parser;
    VCFlags::DefineFlags(&flag_parser);
    FlagParser::Flag* flag_show_include =
        flag_parser.AddBoolFlag("showIncludes");
    FlagParser::Flag* flag_z7 = flag_parser.AddBoolFlag("Z7");
    FlagParser::Flag* flag_zi = flag_parser.AddBoolFlag("Zi");
    FlagParser::Flag* flag_zI = flag_parser.AddBoolFlag("ZI");
    FlagParser::Flag* flag_fprofile_instr_generate = flag_parser.AddBoolFlag(
        "fprofile-instr-generate");
    FlagParser::Flag* flag_fcoverage_mapping = flag_parser.AddBoolFlag(
        "fcoverage-mapping");
    flag_parser.Parse(args);

    if (flag_show_include->seen()) {
      // With this option, full path dependency would be shown in
      // stdout.  We must preserve cwd and all input file paths.
      keep_cwd |= kAsIs;
      keep_pathnames_in_input |= kAsIs;
    }
    if (flag_z7->seen() || flag_zi->seen() || flag_zI->seen()) {
      // If debug info option is set, we must keep cwd, args, pathnames,
      // system include dirs as-is.
      keep_cwd |= kAsIs;
      keep_args |= kAsIs;
      keep_pathnames_in_input |= kAsIs;
      keep_system_include_dirs |= kAsIs;
    }
    if (is_clang_cl
        && flag_fprofile_instr_generate->seen()
        && flag_fcoverage_mapping->seen()) {
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
  } else if (JavacFlags::IsJavacCommand(req->command_spec().name())) {
    keep_cwd = kOmit;
    // It would be OK to normalize args (e.g. in classname) for Javac.
    // However, currently normalizer considers only gcc (clang) args.
    // So, don't normalize.
    keep_args = kAsIs;
    keep_pathnames_in_input = kOmit;
    keep_system_include_dirs = kOmit;
  } else {
    keep_cwd |= kAsIs;
    keep_args |= kAsIs;
    keep_pathnames_in_input |= kAsIs;
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

  Config config;
  config.keep_cwd = keep_cwd;
  config.keep_args = keep_args;
  config.keep_pathnames_in_input = keep_pathnames_in_input;
  config.keep_system_include_dirs = keep_system_include_dirs;
  return config;
}

void ConfigurableExecReqNormalizer::Normalize(
    int id,
    const std::vector<string>& args,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    ExecReq* req) const {
  Config config =
      Configure(id, args, normalize_include_path, is_linking,
                normalize_weak_relative_for_arg, debug_prefix_map, req);

  LOG(INFO) << id << ": normalize:"
            << " keep_cwd=" << config.keep_cwd
            << " keep_args=" << config.keep_args
            << " keep_pathnames_in_input=" << config.keep_pathnames_in_input
            << " keep_system_include_dirs=" << config.keep_system_include_dirs;

  string debug_prefix_map_signature;
  if (!debug_prefix_map.empty()) {
    debug_prefix_map_signature += "debug_prefix_map:";
    for (const auto& iter : debug_prefix_map) {
      debug_prefix_map_signature += iter.second;
      debug_prefix_map_signature += ",";
    }
  }

  // TODO: confirm output does not contains path in include_path
  // for the situation we normalize the include path name.

  NormalizeExecReqSystemIncludeDirs(config.keep_system_include_dirs,
                                    debug_prefix_map,
                                    debug_prefix_map_signature, req);
  NormalizeExecReqArgs(config.keep_args, args, normalize_weak_relative_for_arg,
                       debug_prefix_map, debug_prefix_map_signature, req);
  // This method needs cwd and filename in ExecReq_Input.
  // So, do before processing keep_pathnames and keep_cwd.
  NormalizeExecReqInputOrderForCacheKey(req);
  NormalizeExecReqPathnamesInInput(config.keep_pathnames_in_input,
                                   debug_prefix_map, debug_prefix_map_signature,
                                   req);
  NormalizeExecReqCwd(config.keep_cwd, debug_prefix_map,
                      debug_prefix_map_signature, req);

  NormalizeExecReqSubprograms(req);
  NormalizeExecReqEnvs(req);
}

}  // namespace devtools_goma
