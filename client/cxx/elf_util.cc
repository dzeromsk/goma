// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "elf_util.h"

#include "absl/strings/match.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "path.h"
#include "path_resolver.h"
#include "util.h"

namespace {

absl::string_view GetContentInBrackets(absl::string_view line) {
  absl::string_view::size_type pos = line.find('[');
  line.remove_prefix(pos + 1);
  pos = line.find(']');
  return line.substr(0, pos);
}

std::string FindLibInternal(const string& cwd,
                            absl::string_view dir,
                            const absl::string_view lib_filename,
                            const absl::string_view origin) {
  std::string new_dir = absl::StrReplaceAll(dir, {
                                                     {"$ORIGIN", origin},
                                                 });
  if (devtools_goma::PathResolver::ResolvePath(new_dir) ==
      devtools_goma::PathResolver::ResolvePath(origin)) {
    dir = origin;
  } else {
    dir = new_dir;
  }
  if (absl::StrContains(dir, "$")) {
    LOG(ERROR) << "found non supported $ pattern."
               << " dir=" << dir;
    return string();
  }
  std::string path = file::JoinPathRespectAbsolute(dir, lib_filename);
  if (access(file::JoinPathRespectAbsolute(cwd, path).c_str(), X_OK) == 0) {
    return path;
  }
  return string();
}

}  // namespace

namespace devtools_goma {

bool ElfDepParser::GetDeps(const absl::string_view cmd_or_lib,
                           absl::flat_hash_set<std::string>* deps) {
  // TODO: can we expect readelf always exists in /usr/bin?
  std::vector<std::string> readelf_argv = {"/usr/bin/readelf", "-d",
                                           string(cmd_or_lib)};
  int32_t status;
  string output = ReadCommandOutput(readelf_argv[0], readelf_argv,
                                    std::vector<std::string>(), cwd_,
                                    MERGE_STDOUT_STDERR, &status);
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput readelf with non-zero exit status code."
               << " cmd_or_lib=" << cmd_or_lib << " output=" << output
               << " status=" << status;
    return false;
  }
  std::vector<absl::string_view> libs;
  std::vector<absl::string_view> rpaths;
  if (!ParseReadElf(output, &libs, &rpaths)) {
    LOG(ERROR) << "failed to parse readelf result."
               << " output=" << output;
    return false;
  }

  // keep libs for bredth first search.
  std::vector<std::string> libs_to_search;
  for (const auto& lib : libs) {
    std::string lib_path = FindLib(lib, file::Dirname(cmd_or_lib), rpaths);
    if (lib_path.empty()) {
      LOG(ERROR) << "failed to find dependent library."
                 << " lib=" << lib << " rpaths=" << rpaths
                 << " default_search_path=" << default_search_paths_;
      return false;
    }
    // No need to see a known library.
    if (deps->contains(lib_path)) {
      continue;
    }
    CHECK(deps->insert(lib_path).second);
    libs_to_search.push_back(std::move(lib_path));
  }
  for (const auto& lib : libs_to_search) {
    if (!GetDeps(lib, deps)) {
      return false;
    }
  }

  return true;
}

std::string ElfDepParser::FindLib(
    const absl::string_view lib_filename,
    const absl::string_view origin,
    const std::vector<absl::string_view>& search_paths) const {
  // According to GNU ls.so manual, libraries are searched in following order:
  // 1. DT_RPATH (if --inhibit-cache is not empty string or ':' and no
  //    DT_RUNPATH)
  // 2. LD_LIBRARY_PATH (which can be overwritten by --library-path)
  //    The value should be passed via |default_search_path|.
  // 3. DT_RUNPATH (we do not support this)
  // 4. path in ldconfig cache (we do not support this)
  // 5. trusted default paths (we do not support this)
  if (!ignore_rpath_) {
    for (const auto& dir : search_paths) {
      std::string lib = FindLibInternal(cwd_, dir, lib_filename, origin);
      if (!lib.empty()) {
        return lib;
      }
    }
  }
  for (const std::string& dir : default_search_paths_) {
    std::string lib = FindLibInternal(cwd_, dir, lib_filename, origin);
    if (!lib.empty()) {
      return lib;
    }
  }
  return string();
}

/* static */
bool ElfDepParser::ParseReadElf(absl::string_view content,
                                std::vector<absl::string_view>* libs,
                                std::vector<absl::string_view>* rpaths) {
  DCHECK(libs);
  DCHECK(rpaths);

  static constexpr absl::string_view kSharedLibrary = "Shared library:";
  static constexpr absl::string_view kLibraryRPath = "Library rpath:";

  for (absl::string_view line :
       absl::StrSplit(content, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
    if (absl::StrContains(line, kSharedLibrary)) {
      absl::string_view lib = GetContentInBrackets(line);
      if (lib.empty()) {
        LOG(ERROR) << "unexpected shared library line found: " << line;
        return false;
      }
      libs->push_back(std::move(lib));
    } else if (absl::StrContains(line, kLibraryRPath)) {
      absl::string_view rpath = GetContentInBrackets(line);
      if (rpath.empty()) {
        LOG(ERROR) << "unexpected rpath line found: " << line;
        return false;
      }
      rpaths->push_back(std::move(rpath));
    }
  }
  return true;
}

std::vector<std::string> ParseLdSoConf(absl::string_view content) {
  std::vector<std::string> ret;

  for (absl::string_view line :
       absl::StrSplit(content, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
    // Omit anything after '#'.
    absl::string_view::size_type pos = line.find('#');
    line = line.substr(0, pos);
    line = absl::StripAsciiWhitespace(line);
    if (line.empty()) {
      continue;
    }
    // TODO: support include and hwcap if we need.
    if (absl::StartsWith(line, "include") || absl::StartsWith(line, "hwcap")) {
      LOG(WARNING) << "non supported line:" << line;
      continue;
    }
    ret.push_back(string(line));
  }
  return ret;
}

}  // namespace devtools_goma
