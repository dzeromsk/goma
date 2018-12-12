// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "processor.h"

#include "absl/time/clock.h"
#include "base/path.h"
#include "client/content.h"
#include "lexer.h"
#include "lib/path_resolver.h"
#include "parser.h"
#include "token.h"

namespace devtools_goma {
namespace modulemap {

bool Processor::AddModuleMapFile(const absl::string_view module_map_file) {
  const string abs_module_map_file = PathResolver::ResolvePath(
      file::JoinPathRespectAbsolute(cwd_, module_map_file));
  if (visited_abs_paths_.count(abs_module_map_file) > 0) {
    // already processed.
    return true;
  }

  FileStat stat = file_stat_cache_->Get(abs_module_map_file);
  if (!stat.IsValid() || stat.is_directory) {
    LOG(WARNING) << "failed to read " << abs_module_map_file;
    return false;
  }

  collected_module_map_files_.emplace_back(string(module_map_file),
                                           abs_module_map_file, stat);
  visited_abs_paths_.emplace(abs_module_map_file);

  // Parse modulemap.
  std::unique_ptr<Content> content =
      Content::CreateFromFile(abs_module_map_file);
  if (!content) {
    // file is missing or directory. When we come here, we expect a file
    // exists. But not?
    LOG(WARNING) << "failed to read " << abs_module_map_file;
    return false;
  }

  std::vector<Token> tokens;
  if (!Lexer::Run(*content, &tokens)) {
    LOG(WARNING) << "failed to run lexing " << abs_module_map_file;
    return false;
  }

  ModuleMap module_map;
  if (!Parser::Run(tokens, &module_map)) {
    LOG(WARNING) << "failed to run parsing " << abs_module_map_file;
    return false;
  }

  absl::string_view module_map_dir = file::Dirname(module_map_file);
  for (const auto& module_decl : module_map.modules()) {
    if (!AddExternMapduleMapFilesRecursively(module_decl, module_map_dir)) {
      return false;
    }
  }

  return true;
}

bool Processor::AddExternMapduleMapFilesRecursively(
    const Module& module_decl,
    const absl::string_view module_map_dir) {
  // If the module is `extern module` form, extern_filename exists.
  if (!module_decl.extern_filename().empty()) {
    string rel_path = file::JoinPathRespectAbsolute(
        module_map_dir, module_decl.extern_filename());
    if (!AddModuleMapFile(rel_path)) {
      return false;
    }
  }

  for (const auto& submodule : module_decl.submodules()) {
    if (!AddExternMapduleMapFilesRecursively(submodule, module_map_dir)) {
      return false;
    }
  }

  return true;
}

}  // namespace modulemap
}  // namespace devtools_goma
