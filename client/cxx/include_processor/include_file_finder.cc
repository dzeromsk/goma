// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include_file_finder.h"

#include <utility>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "counterz.h"
#include "cpp_parser.h"
#include "file_dir.h"
#include "file_stat_cache.h"
#include "include_file_utils.h"
#include "list_dir_cache.h"
#include "path.h"
#include "path_resolver.h"

namespace devtools_goma {

namespace {

// TODO: Merge with CleanPathSep(..) in include_dir_cache.cc.
string RemoveDuplicateSlash(const string& path) {
  string res;
  res.reserve(path.size());
  for (const auto& ch : path) {
    if (ch == '/' && !res.empty() && res.back() == '/') {
      continue;
    }
    res += ch;
  }
  return res;
}

}  // anonymous namespace

bool IncludeFileFinder::gch_hack_ = false;

/* static */
void IncludeFileFinder::Init(bool gch_hack) {
  gch_hack_ = gch_hack;
}

IncludeFileFinder::IncludeFileFinder(string cwd,
                                     bool ignore_case,
                                     const std::vector<string>* include_dirs,
                                     const std::vector<string>* framework_dirs,
                                     FileStatCache* file_stat_cache)
    : cwd_(std::move(cwd)),
      ignore_case_(ignore_case),
      include_dirs_(include_dirs),
      framework_dirs_(framework_dirs),
      file_stat_cache_(file_stat_cache) {
  GOMA_COUNTERZ("IncludeFileFinder");

  files_in_include_dirs_.resize(include_dirs_->size());

  // Enumerate all files and directories in each of |include_dirs|.
  // Files and directories are used to skip unnecessary file checks.
  for (size_t i = CppParser::kIncludeDirIndexStarting;
       i < include_dirs_->size(); ++i) {
    const string& abs_include_dir =
        file::JoinPathRespectAbsolute(cwd_, (*include_dirs)[i]);
    if (absl::EndsWith(abs_include_dir, ".hmap")) {
      std::vector<std::pair<string, string>> entries;
      if (!ReadHeaderMapContent(abs_include_dir, &entries)) {
        LOG(WARNING) << "failed to load header map:" << abs_include_dir;
        continue;
      }

      for (const auto& entry : entries) {
        const string& key = entry.first;
        const string& filename = entry.second;

        string top = TopPathComponent(key, ignore_case_);

        files_in_include_dirs_[i].insert(top);

        include_dir_index_lowerbound_.emplace(std::move(top), i);

        hmap_map_.insert(std::make_pair(
            std::make_pair(i, key), filename));
      }
      continue;
    }

    std::vector<DirEntry> entries;
    if (!ListDirCache::instance()->GetDirEntries(
            abs_include_dir, file_stat_cache_->Get(abs_include_dir),
            &entries)) {
      continue;
    }

    for (const auto& entry : entries) {
      string name = entry.name;

      if (ignore_case_) {
        absl::AsciiStrToLower(&name);
      }

      files_in_include_dirs_[i].insert(name);

      include_dir_index_lowerbound_.emplace(std::move(name), i);
    }
  }
}

/* static */
string IncludeFileFinder::TopPathComponent(string path_in_directive,
                                           bool ignore_case) {
  string::size_type slash_pos = string::npos;
  if (ignore_case) {
    absl::AsciiStrToLower(&path_in_directive);
    // Since some Windows SDK has a include like "foo\\bar",
    // we need to support this.
    slash_pos = path_in_directive.find_first_of("\\/");
  } else {
    slash_pos = path_in_directive.find("/");
  }

  if (slash_pos != string::npos) {
    path_in_directive = path_in_directive.substr(0, slash_pos);
  }

  return path_in_directive;
}

bool IncludeFileFinder::Lookup(
    const string& path_in_directive,
    string* filepath,
    int* include_dir_index) {
  GOMA_COUNTERZ("Lookup");
  VLOG(2) << "Lookup=" << path_in_directive;

  {
    // Check cache.
    auto iter = include_path_cache_.find(
        std::make_pair(path_in_directive, *include_dir_index));
    if (iter != include_path_cache_.end()) {
      *filepath = iter->second.first;
      *include_dir_index = iter->second.second;
      return true;
    }
  }

  // |top| is used to reduce the number of searched include directories
  // by checking precalculated direct children of include dirs.
  // e.g. if #include <foo/bar.h> comes, include directories not having
  // foo directory are not searched.
  string top = TopPathComponent(path_in_directive, ignore_case_);
  VLOG(2) << "top=" << top;

  size_t search_start_index = *include_dir_index;

  {
    // Include dirs with less than search_start_index should not have
    // |path_in_directive|.
    // e.g. if |top| is "base" and 1,2,3-th include directories do not
    // have "base" entry, then search_start_index becomes 4.
    auto iter = include_dir_index_lowerbound_.find(top);
    if (iter != include_dir_index_lowerbound_.end()) {
      search_start_index = std::max(search_start_index, iter->second);
    } else if (!gch_hack_enabled() &&
               !absl::StartsWith(path_in_directive, ".")) {
      // Do not search entry that is not in include_dirs.
      // If |top| is not in |ininclude_dir_index_lowerbound_|,
      // it means that |path_in_directive| is not in include directories.
      // This happens for Mac framework headers.
      // If |path_in_directive| starts with ".",
      // we need to search all include_dirs.
      return LookupFramework(path_in_directive, filepath);
    }
  }

  for (size_t i = search_start_index; i < include_dirs_->size(); ++i) {
    // If |top| entry is not in i-th include dirs, check is skipped.
    //
    // |files_in_include_dirs_| only holds file/directory name
    // in each include directory.
    // If |top| starts from "." or "..", cannot skip include directory check
    // because it may point to some sibling directory
    // that not in |files_in_include_dirs_|.
    if (!absl::StartsWith(top, ".") &&
        files_in_include_dirs_[i].find(top) ==
        files_in_include_dirs_[i].end()) {
      VLOG(2) << "not in " << i;
      continue;
    }

    string join_path;
    {
      auto iter = hmap_map_.find(std::make_pair(i, path_in_directive));
      if (iter != hmap_map_.end()) {
        join_path = iter->second;
      } else {
        join_path = file::JoinPath((*include_dirs_)[i], path_in_directive);
      }
    }
    string try_path;
    PathResolver::PlatformConvertToString(join_path, &try_path);
    try_path = RemoveDuplicateSlash(try_path);
    VLOG(2) << "try_path=" << try_path;

    if (gch_hack_enabled()) {
      const string& gch_path = try_path + GOMA_GCH_SUFFIX;
      FileStat filestat =
          file_stat_cache_->Get(file::JoinPathRespectAbsolute(cwd_, gch_path));
      if (!filestat.is_directory && filestat.IsValid()) {
        *filepath = gch_path;
        *include_dir_index = i;
        return true;
      }
    }

    const string full_try_path = file::JoinPathRespectAbsolute(cwd_, try_path);
    FileStat filestat = file_stat_cache_->Get(full_try_path);
    if (filestat.is_directory || !filestat.IsValid()) {
      VLOG(2) << "filestat error:" << full_try_path
              << " " << filestat.DebugString();
      continue;
    }

    include_path_cache_.insert(
        std::make_pair(
            std::make_pair(path_in_directive, *include_dir_index),
            std::make_pair(try_path, i)));
    *filepath = try_path;
    *include_dir_index = i;
    return true;
  }

  return LookupFramework(path_in_directive, filepath);
}

bool IncludeFileFinder::LookupFramework(const string& path_in_directive,
                                        string* filepath) {
  auto sep_pos = path_in_directive.find('/');
  if (sep_pos == string::npos) {
    return false;
  }

  const string framework_name =
      path_in_directive.substr(0, sep_pos) + ".framework";
  const string base_name = path_in_directive.substr(sep_pos + 1);

  for (const auto& framework_dir : *framework_dirs_) {
    for (const auto& header_dir : {"Headers", "PrivateHeaders"}) {
      const string filename =
          file::JoinPath(framework_dir, framework_name, header_dir, base_name);
      const FileStat filestat =
          file_stat_cache_->Get(file::JoinPathRespectAbsolute(cwd_, filename));
      if (!filestat.is_directory && filestat.IsValid()) {
        *filepath = filename;
        return true;
      }
    }
  }
  return false;
}

bool IncludeFileFinder::LookupSubframework(const string& path_in_directive,
                                           const string& current_directory,
                                           string* filepath) {
  const string& abs_current =
      file::JoinPathRespectAbsolute(cwd_, current_directory);
  for (const auto& fwdir : *framework_dirs_) {
    if (CreateSubframeworkIncludeFilename(
            file::JoinPathRespectAbsolute(cwd_, fwdir),
            abs_current, path_in_directive, filepath)) {
      return true;
    }
  }
  return false;
}

}  // namespace devtools_goma
