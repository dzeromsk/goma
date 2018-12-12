// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_PROCESSOR_H_
#define DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_PROCESSOR_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "file_stat.h"
#include "file_stat_cache.h"
#include "type.h"

using std::string;

namespace devtools_goma {
namespace modulemap {

struct CollectedModuleMapFile {
  CollectedModuleMapFile(string rel_path, string abs_path, FileStat file_stat)
      : rel_path(std::move(rel_path)),
        abs_path(std::move(abs_path)),
        file_stat(std::move(file_stat)) {}

  // An relative path from cwd.
  // Possible absolute if an absolute path is specified as a module map file.
  string rel_path;
  string abs_path;
  FileStat file_stat;
};

// Processor parses a modulemap file, and lists all extern modulemap files.
class Processor {
 public:
  Processor(string cwd, FileStatCache* file_stat_cache)
      : cwd_(std::move(cwd)), file_stat_cache_(file_stat_cache) {}

  Processor(const Processor&) = delete;
  void operator=(const Processor&) = delete;

  // Reads and parses a module map file, and collects linked module map files.
  //
  // |module_map_file| is either a relative path from |cwd| or
  // an absolute path. If it's in relative form, a relative path is collected.
  //
  // Returns true if succeeded (or |module_map_file| is ignored since
  // it's already processed before).
  // Returns false otherwise (e.g. parse failure)
  bool AddModuleMapFile(absl::string_view module_map_file);

  const std::vector<CollectedModuleMapFile>& collected_module_map_files()
      const {
    return collected_module_map_files_;
  }

  std::vector<CollectedModuleMapFile>* mutable_collected_module_map_files() {
    return &collected_module_map_files_;
  }

 private:
  // Finds `extern module ...` from |module_decl|, and add them.
  bool AddExternMapduleMapFilesRecursively(const Module& module_decl,
                                           absl::string_view module_map_dir);

  const string cwd_;
  FileStatCache* file_stat_cache_;

  std::vector<CollectedModuleMapFile> collected_module_map_files_;
  absl::flat_hash_set<string> visited_abs_paths_;
};

}  // namespace modulemap
}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_PROCESSOR_H_
