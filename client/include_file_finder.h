// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_INCLUDE_FILE_FINDER_H_
#define DEVTOOLS_GOMA_CLIENT_INCLUDE_FILE_FINDER_H_

#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devtools_goma {

class FileIdCache;

class IncludeFileFinder {
 public:
  static void Init(bool gch_hack);
  static bool gch_hack_enabled() {
    return gch_hack_;
  }

  IncludeFileFinder(const IncludeFileFinder&) = delete;
  IncludeFileFinder& operator=(const IncludeFileFinder&) = delete;

  IncludeFileFinder(const std::string& cwd,
                    bool ignore_case,
                    const std::vector<std::string>* include_dirs,
                    const std::vector<std::string>* framework_dirs,
                    FileIdCache* file_id_cache);

  // Search included file and set to |filepath| if path is found.
  // If |path_in_directive| is found in an include directory,
  // Lookup(...) returns true.
  bool Lookup(const std::string& path_in_directive, std::string* filepath,
              int* include_dir_index);

  // Calculate |top| component in include directive.
  // e.g.
  // #include <foo/bar.h> -> |top| is "foo"
  // #include "bar.h" -> |top| is "bar.h"
  // #include <hoge\\fuga.h> -> |top| is "hoge"
  // #include <foo/bar/baz.h> -> |top| is "foo"
  // #include "../bar.h" -> |top| is ".."
  // #include <foo\\bar\\baz.h> -> |top| is "foo"
  // #include <WinBase.h> -> |top| is "winbase.h" in Windows
  static std::string TopPathComponent(std::string path_in_directive,
                                      bool ignore_case);

  // TODO: Make this function private
  // when we can stop fallback to IncludeDirCache.
  bool LookupSubframework(const std::string& path_in_directive,
                          const std::string& current_directory,
                          std::string* filepath);

 private:
  bool LookupFramework(const std::string& path_in_directive,
                       std::string* filepath);

  static bool gch_hack_;

  const std::string cwd_;
  const bool ignore_case_;
  const std::vector<std::string>* const include_dirs_;
  const std::vector<std::string>* const framework_dirs_;
  FileIdCache* file_id_cache_;

  // Holds entries in i-th include directory.
  // |files_in_include_dirs_[i]| is set of file/directory name in
  // i-th include directory.
  std::vector<std::unordered_set<std::string>> files_in_include_dirs_;

  // Holds the minimum include directories index for each entries in
  // include directories.
  // e.g. |include_dir_index_lowerbound_["stdio.h"]| represents minimum index
  // of include directory containing "stdio.h".
  std::unordered_map<std::string, size_t> include_dir_index_lowerbound_;

  // Cache for (path_in_directive, include_dir_index_start) ->
  //           (filepath, used_include_dir_index).
  std::map<std::pair<std::string, int>,
           std::pair<std::string, int>> include_path_cache_;

  // Map for "include_dir idx + (key in .hmap file)" -> filename in .hmap file.
  std::map<std::pair<int, std::string>, std::string> hmap_map_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_INCLUDE_FILE_FINDER_H_
