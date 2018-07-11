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
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "path.h"
#include "path_resolver.h"
#include "path_util.h"
using ::google::protobuf::RepeatedPtrField;
using ::absl::StrCat;

namespace devtools_goma {

string FixPathToBeCwdRelative::ParseFlagValue(const FlagParser::Flag& flag,
                                              const string& value) {
  string normalized_path = PathResolver::WeakRelativePath(value, cwd_);
  if (normalized_path != value) {
    is_fixed_ = true;
  }
  return normalized_path;
}

string PathRewriterWithDebugPrefixMap::ParseFlagValue(
    const FlagParser::Flag& flag,
    const string& value) {
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
  DCHECK(keep_args & kAsIs) << keep_args;
  LOG_IF(ERROR, (keep_args & kAsIs) == 0)
      << "NormalizeExecReqArgs's default implementation is not provided. "
      << "keep_args must have kAsIs. To implement normalization, provide "
      << "compiler specific one.";
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
    const absl::optional<string>& new_cwd,
    const std::map<string, string>& debug_prefix_map,
    const string& debug_prefix_map_signature,
    ExecReq* req) const {
  if (keep_cwd & kAsIs) {
    return;
  }

  bool is_rewritten = false;
  bool is_removed = false;
  bool is_replaced = false;

  static const char kPwd[] = "PWD=";

  if (keep_cwd & kNormalizeWithDebugPrefixMap) {
    // If there is PWD= in env, replace cwd with content of PWD=.
    for (const auto& env_var : req->env()) {
      if (absl::StartsWith(env_var, kPwd)) {
        *req->mutable_cwd() = env_var.substr(strlen(kPwd));
        break;
      }
    }

    if (new_cwd) {
      // fdebug-compilation-dir is applied before fdebug-prefix-map when we use
      // fdebug-prefix-map.
      req->set_cwd(*new_cwd);
      is_replaced = true;
    }

    RewritePathWithDebugPrefixMap(debug_prefix_map, req->mutable_cwd());
    is_rewritten = true;
  } else if (new_cwd) {
    req->set_cwd(*new_cwd);
    is_replaced = true;
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
  if (is_replaced) {
    normalized_spec->mutable_comment()->append(" cwd:replaced");
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

void ConfigurableExecReqNormalizer::NormalizeForCacheKey(
    int id,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    ExecReq* req) const {
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
  NormalizeExecReqCwd(config.keep_cwd, config.new_cwd, debug_prefix_map,
                      debug_prefix_map_signature, req);

  NormalizeExecReqSubprograms(req);
  NormalizeExecReqEnvs(req);
}

ConfigurableExecReqNormalizer::Config AsIsExecReqNormalizer::Configure(
    int id,
    const std::vector<string>& args,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    const ExecReq* req) const {
  return Config::AsIs();
}

}  // namespace devtools_goma
