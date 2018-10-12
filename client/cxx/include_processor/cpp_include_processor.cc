// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef _WIN32
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include "config_win.h"
#endif
#ifdef __FreeBSD__
#include <sys/param.h>
#endif

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "autolock_timer.h"
#include "clang_tidy_flags.h"
#include "compiler_flags.h"
#include "compiler_info.h"
#include "content.h"
#include "counterz.h"
#include "cpp_directive.h"
#include "cpp_directive_parser.h"
#include "cpp_include_processor.h"
#include "cpp_parser.h"
#include "directive_filter.h"
#include "env_flags.h"
#include "file_dir.h"
#include "filesystem.h"
#include "flag_parser.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "glog/vlog_is_on.h"
#include "include_cache.h"
#include "include_file_utils.h"
#include "ioutil.h"
#include "list_dir_cache.h"
#include "lockhelper.h"
#include "path.h"
#include "path_resolver.h"
#include "scoped_fd.h"
#include "util.h"
#include "vc_flags.h"

#ifdef _WIN32
#include "path_resolver.h"
#include "posix_helper_win.h"
#endif

using std::string;

namespace devtools_goma {

namespace {

// Reads content from |filepath| and set |next_current_directory|.
// If |file_stat_cache| has a FileStat for |filepath|, we use it.
// Otherwise we take FileStat for |filepath| and stores it to |file_stat_cache|.
// We don't take |file_stat_cache| ownership.
IncludeItem TryInclude(const string& cwd,
                       const string& filepath,
                       string* next_current_directory,
                       FileStatCache* file_stat_cache) {
  GOMA_COUNTERZ("TryInclude");

  const string abs_filepath = file::JoinPathRespectAbsolute(cwd, filepath);
  FileStat file_stat(file_stat_cache->Get(abs_filepath));
  if (!file_stat.IsValid()) {
    return IncludeItem();
  }

  if (file_stat.is_directory) {
    VLOG(2) << "TryInclude but dir:" << abs_filepath;
    return IncludeItem();
  }

  CHECK(IncludeCache::IsEnabled())
      << "IncludeCache is not enabled. "
      << "You forget to call IncludeCache::Init()?";

  *next_current_directory = string(file::Dirname(filepath));
  return IncludeCache::instance()->GetIncludeItem(abs_filepath, file_stat);
}

}  // anonymous namespace

class IncludePathsObserver : public CppParser::IncludeObserver {
 public:
  IncludePathsObserver(std::string cwd,
                       CppParser* parser,
                       std::set<string>* shared_include_files,
                       FileStatCache* file_stat_cache,
                       IncludeFileFinder* include_file_finder)
      : cwd_(std::move(cwd)),
        parser_(parser),
        shared_include_files_(shared_include_files),
        file_stat_cache_(file_stat_cache),
        include_file_finder_(include_file_finder) {
    CHECK(parser_);
    CHECK(shared_include_files_);
    CHECK(file_stat_cache_);
  }

  bool HandleInclude(const string& path,
                     const string& current_directory,
                     const string& current_filepath,
                     char quote_char,  // '"' or '<'
                     int include_dir_index) override {
    // shared_include_files_ contains a set of include files for compilers.
    // It's output variables of IncludePathsObserver.

    // parser_->IsProcessedFile(filepath) indicates filepath was already parsed
    // and no need to parse it again.
    // So, if it returns true, shared_include_files_ must have filepath.
    // In other words, there is a case shared_include_files_ have the filepath,
    // but parser_->IsProcessedFile(filepath) returns false.  It means
    // the filepath once parsed, but it needs to parse it again (for example
    // macro changed).

    // parser_->AddFileInput should be called to let parser_ parse the file.

    // include_dir_index is an index to start searching from.
    //
    // for #include "...", include_dir_index is current dir index of
    // the file that is including the path.  note that include_dir_index
    // would not be kCurrentDirIncludeDirIndex, since CppParser needs
    // to keep dir index for include file. i.e. an included file will have
    // the same include dir index as file that includes the file.
    //
    // for #include <...>, it is bracket_include_dir_index.
    //
    // for #include_next, it will be next include dir index of file
    // that is including the path. (always quote_char=='<').

    CHECK(!path.empty()) << current_filepath;

    VLOG(2) << current_filepath << ": including " << quote_char << path
            << " dir:" << current_directory
            << " include_dir_index:" << include_dir_index;

    string next_current_directory;
    string filepath;

    if (quote_char == '"') {
      // Look for current directory.
      if (HandleIncludeInDir(current_directory, path, include_dir_index,
                             &next_current_directory)) {
        return true;
      }
      VLOG(2) << "not found in curdir:" << current_directory;

      // If not found in current directory, try all include paths.
      include_dir_index = CppParser::kIncludeDirIndexStarting;
    }

    // Look for include dirs from |include_dir_index|.
    int dir_index = include_dir_index;
    if (!include_file_finder_->Lookup(path, &filepath, &dir_index) &&
        !include_file_finder_->LookupSubframework(path, current_directory,
                                                  &filepath)) {
      VLOG(2) << "Not found: " << path;
      return false;
    }

    VLOG(3) << "Lookup => " << filepath << " dir_index=" << dir_index;

    if (parser_->IsProcessedFile(filepath, include_dir_index)) {
      VLOG(2) << "Already processed:" << quote_char << filepath;
      return true;
    }

    IncludeItem include_item =
        TryInclude(cwd_, filepath, &next_current_directory, file_stat_cache_);
    if (include_item.IsValid()) {
      if (IncludeFileFinder::gch_hack_enabled() &&
          absl::EndsWith(filepath, GOMA_GCH_SUFFIX) &&
          !absl::EndsWith(path, GOMA_GCH_SUFFIX)) {
        VLOG(2) << "Found a precompiled header: " << filepath;
        shared_include_files_->insert(filepath);
        return true;
      }

      VLOG(2) << "Looking into " << filepath << " index=" << dir_index;
      shared_include_files_->insert(filepath);
      parser_->AddFileInput(std::move(include_item), filepath,
                            next_current_directory, dir_index);
      return true;
    }
    VLOG(2) << "include file not found in dir_cache?";
    return false;
  }

  bool HasInclude(const string& path,
                  const string& current_directory,
                  const string& current_filepath,
                  char quote_char,  // '"' or '<'
                  int include_dir_index) override {
    CHECK(!path.empty()) << current_filepath;

    string next_current_directory;
    string filepath;

    if (quote_char == '"') {
      if (HasIncludeInDir(current_directory, path, current_filepath)) {
        return true;
      }
      include_dir_index = CppParser::kIncludeDirIndexStarting;
    }

    int dir_index = include_dir_index;
    if (!include_file_finder_->Lookup(path, &filepath, &dir_index)) {
      VLOG(2) << "Not found: " << path;
      return false;
    }
    std::string abs_filepath =
        file::JoinPathRespectAbsolute(cwd_, filepath);
    if (shared_include_files_->count(filepath) ||
        access(abs_filepath.c_str(), R_OK) == 0) {
      DCHECK(!file::IsDirectory(abs_filepath, file::Defaults()).ok())
          << abs_filepath;
      shared_include_files_->insert(std::move(filepath));
      return true;
    }
    return false;
  }

 private:
  bool CanPruneWithTopPathComponent(const string& dir, const string& path) {
    // we don't need to care about case ignoreness here.
    // we'll access filesystem, so filesystem would handle case senstiveness.
    const std::string& dir_with_top_path_component = file::JoinPath(
        dir, IncludeFileFinder::TopPathComponent(path, false));
    return !file_stat_cache_->Get(dir_with_top_path_component).IsValid();
  }

  bool HandleIncludeInDir(const string& dir,
                          const string& path,
                          int include_dir_index,
                          string* next_current_directory) {
    GOMA_COUNTERZ("handle include try");
    if (CanPruneWithTopPathComponent(file::JoinPathRespectAbsolute(cwd_, dir),
                                     path)) {
      VLOG(2) << "can prune with top path component:"
              << " cwd=" << cwd_
              << " dir=" << dir
              << " path=" << path;
      GOMA_COUNTERZ("handle include pruned");
      return false;
    }

    string filepath =
        PathResolver::PlatformConvert(file::JoinPathRespectAbsolute(dir, path));

    VLOG(2) << "handle include in dir: " << filepath;

    if (IncludeFileFinder::gch_hack_enabled()) {
      const string& gchpath = filepath + GOMA_GCH_SUFFIX;
      IncludeItem include_item(
          TryInclude(cwd_, gchpath, next_current_directory, file_stat_cache_));
      if (include_item.IsValid()) {
        VLOG(2) << "Found a pre-compiled header: " << gchpath;
        shared_include_files_->insert(gchpath);
        // We should not check the content of pre-compiled headers.
        return true;
      }
    }

    if (parser_->IsProcessedFile(filepath, include_dir_index)) {
      VLOG(2) << "Already processed: \"" << filepath << "\"";
      return true;
    }
    IncludeItem include_item =
        TryInclude(cwd_, filepath, next_current_directory, file_stat_cache_);
    if (include_item.IsValid()) {
      shared_include_files_->insert(filepath);
      parser_->AddFileInput(std::move(include_item), filepath,
                            *next_current_directory, include_dir_index);
      return true;
    }
    VLOG(2) << "include file not found in current directoy? filepath="
            << filepath;
    return false;
  }

  bool HasIncludeInDir(const string& dir,
                       const string& path,
                       const string& current_filepath) {
    std::string filepath = file::JoinPathRespectAbsolute(dir, path);
    string abs_filepath = file::JoinPathRespectAbsolute(cwd_, filepath);
    string abs_current_filepath =
        file::JoinPathRespectAbsolute(cwd_, current_filepath);
    abs_filepath = PathResolver::ResolvePath(abs_filepath);
    bool is_current = (abs_filepath == abs_current_filepath);
    if (is_current) {
      shared_include_files_->insert(std::move(filepath));
      return true;
    }
    if (!file::IsDirectory(abs_filepath, file::Defaults()).ok()) {
      if (shared_include_files_->count(filepath)) {
        return true;
      }
      if (access(abs_filepath.c_str(), R_OK) == 0) {
        shared_include_files_->insert(std::move(filepath));
        return true;
      }
      if (IncludeFileFinder::gch_hack_enabled() &&
          access((abs_filepath + GOMA_GCH_SUFFIX).c_str(), R_OK) == 0) {
        shared_include_files_->insert(filepath + GOMA_GCH_SUFFIX);
        return true;
      }
    }
    return false;
  }

  const std::string cwd_;
  CppParser* parser_;
  std::set<string>* shared_include_files_;
  FileStatCache* file_stat_cache_;

  IncludeFileFinder* include_file_finder_;

  DISALLOW_COPY_AND_ASSIGN(IncludePathsObserver);
};

class IncludeErrorObserver : public CppParser::ErrorObserver {
 public:
  IncludeErrorObserver() {}

  void HandleError(const string& error) override {
    // Note that we don't set this error observer if VLOG_IS_ON(1) is false.
    // If you need to change this code, make sure you'll modify
    // set_error_observer call in CppIncludeProcessor::GetIncludeFiles()
    // to be consistent with here.
    VLOG(1) << error;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(IncludeErrorObserver);
};

static void CopyIncludeDirs(const std::vector<string>& input_dirs,
                            const string& toolchain_root,
                            std::vector<string>* output_dirs) {
  for (const auto& input_dir : input_dirs) {
    const string& dir = file::JoinPath(
        toolchain_root, PathResolver::PlatformConvert(input_dir));
    output_dirs->push_back(dir);
  }
}

#ifndef _WIN32
static void CopyOriginalFileFromHashCriteria(const string& filepath) {
  static Lock mu;

  if (access(filepath.c_str(), R_OK) == 0) {
    return;
  }

  // Only one thread can copy the GCH.
  AUTOLOCK(lock, &mu);
  if (access(filepath.c_str(), R_OK) == 0) {
    return;
  }

  const string& hash_criteria_filepath = filepath + ".gch.hash-criteria";
  std::ifstream ifs(hash_criteria_filepath.c_str());
  if (!ifs) {
    return;
  }

  string line;
  getline(ifs, line);
  const char* expected_prefix = "Contents of ";
  if (!absl::StartsWith(line, expected_prefix)) {
    return;
  }

  const string& original_filepath = line.substr(strlen(expected_prefix));
  VLOG(1) << "hash criteria file found. original filepath: "
          << original_filepath;

  const string tmp_filepath = filepath + ".tmp";
  file::Copy(original_filepath, tmp_filepath, file::Overwrite());
  rename(tmp_filepath.c_str(), filepath.c_str());
}
#endif

static bool NormalizePath(const string& path_to_normalize,
                          string* normalized_path) {
// TODO: Can't we remove this ifdef? Maybe we have make a code
//                    that is platform independent?
//                    Do we need to resolve symlink on Unix?
#ifndef _WIN32
  std::unique_ptr<char[], decltype(&free)> path_buf(
      realpath(path_to_normalize.c_str(), nullptr), free);
  if (path_buf.get() == nullptr)
    return false;
  normalized_path->assign(path_buf.get());
#else
  *normalized_path = PathResolver::ResolvePath(
      PathResolver::PlatformConvert(path_to_normalize));
  if (normalized_path->empty() ||
      (GetFileAttributesA(normalized_path->c_str()) ==
       INVALID_FILE_ATTRIBUTES)) {
    return false;
  }
#endif
  return true;
}

static void MergeDirs(const std::string cwd,
                      const std::vector<string>& dirs,
                      std::vector<string>* include_dirs,
                      std::set<string>* seen_include_dir_set) {
  for (const auto& dir : dirs) {
    std::string abs_dir = file::JoinPathRespectAbsolute(cwd, dir);
    string normalized_dir;
    if (!NormalizePath(abs_dir, &normalized_dir)) {
      continue;
    }
    // Remove duplicated dirs.
    if (!seen_include_dir_set->insert(normalized_dir).second) {
      continue;
    }
    include_dirs->push_back(dir);
  }
}

static void MergeIncludeDirs(const std::string& cwd,
                             const std::vector<string>& nonsystem_include_dirs,
                             const std::vector<string>& system_include_dirs,
                             std::vector<string>* include_dirs) {
  std::set<string> seen_include_dir_set;

  // We check system include paths first because we should give more
  // priority to system paths than non-system paths when we check
  // duplicates of them. We will push back the system include paths
  // into include_paths later because the order of include paths
  // should be non-system path first.
  std::vector<string> unique_system_include_dirs;
  MergeDirs(cwd, system_include_dirs, &unique_system_include_dirs,
            &seen_include_dir_set);

  MergeDirs(cwd, nonsystem_include_dirs, include_dirs, &seen_include_dir_set);

  copy(unique_system_include_dirs.begin(), unique_system_include_dirs.end(),
       back_inserter(*include_dirs));
}

bool CppIncludeProcessor::GetIncludeFiles(const string& filename,
                                          const string& current_directory,
                                          const CompilerFlags& compiler_flags,
                                          const CxxCompilerInfo& compiler_info,
                                          std::set<string>* include_files,
                                          FileStatCache* file_stat_cache) {
  DCHECK(!current_directory.empty());
  DCHECK(file::IsAbsolutePath(current_directory)) << current_directory;

  std::vector<string> non_system_include_dirs;
  std::vector<string> root_includes;
  std::vector<string> user_framework_dirs;
  std::vector<std::pair<string, bool>> commandline_macros;
#if _WIN32
  bool ignore_case = true;
#else
  bool ignore_case = false;
#endif

  if (compiler_flags.type() == CompilerFlagType::Gcc) {
    const GCCFlags& flags = static_cast<const GCCFlags&>(compiler_flags);
    non_system_include_dirs = flags.non_system_include_dirs();
    root_includes = flags.root_includes();
    user_framework_dirs = flags.framework_dirs();
    commandline_macros = flags.commandline_macros();
  } else if (compiler_flags.type() == CompilerFlagType::Clexe) {
    const VCFlags& flags = static_cast<const VCFlags&>(compiler_flags);
    non_system_include_dirs = flags.include_dirs();
    root_includes = flags.root_includes();
    commandline_macros = flags.commandline_macros();
    // in chromium, clang-cl on linux
    // https://chromium.googlesource.com/chromium/src/+/lkcr/docs/win_cross.md
    // it is expected to use ciopfs for win_sdk, but not for chromium source.
    // (depot_tools configured it so)
    ignore_case = true;
  } else if (compiler_flags.type() == CompilerFlagType::ClangTidy) {
    const ClangTidyFlags& flags =
        static_cast<const ClangTidyFlags&>(compiler_flags);
    non_system_include_dirs = flags.non_system_include_dirs();
    root_includes = flags.root_includes();
    user_framework_dirs = flags.framework_dirs();
    commandline_macros = flags.commandline_macros();
  } else {
    LOG(FATAL) << "Bad compiler_flags for CppIncludeProcessor: "
               << compiler_flags.DebugString();
  }
  VLOG(3) << "non_system_include_dirs=" << non_system_include_dirs;
  VLOG(3) << "root_includes=" << root_includes;
  VLOG(3) << "user_framework_dirs=" << user_framework_dirs;
  VLOG(3) << "commandline_macros=" << commandline_macros;

  for (const auto& include_dir : non_system_include_dirs) {
    // TODO: Ideally, we should not add .hmap file if this
    //               file doesn't exist.
    if (absl::EndsWith(include_dir, ".hmap")) {
      include_files->insert(include_dir);
    }
  }

  std::vector<string> quote_dirs;
  CopyIncludeDirs(compiler_info.quote_include_paths(), "", &quote_dirs);

  std::vector<string> all_system_include_dirs;
  if (compiler_info.lang().find("c++") != string::npos) {
    CopyIncludeDirs(compiler_info.cxx_system_include_paths(),
                    compiler_info.toolchain_root(), &all_system_include_dirs);
  } else {
    CopyIncludeDirs(compiler_info.system_include_paths(),
                    compiler_info.toolchain_root(), &all_system_include_dirs);
  }

  // The first element of include_dirs.include_dirs represents the current input
  // directory. It's not specified by -I, but we need to handle it when
  // including file with #include "".
  std::vector<std::string> include_dirs;
  std::vector<std::string> framework_dirs;
  include_dirs.push_back(current_directory);
  copy(quote_dirs.begin(), quote_dirs.end(), back_inserter(include_dirs));

  cpp_parser_.set_bracket_include_dir_index(include_dirs.size());
  VLOG(2) << "bracket include dir index=" << include_dirs.size();
  MergeIncludeDirs(current_directory, non_system_include_dirs,
                   all_system_include_dirs, &include_dirs);

#ifndef _WIN32
  std::vector<string> abs_user_framework_dirs;
  CopyIncludeDirs(user_framework_dirs, "", &abs_user_framework_dirs);
  std::vector<string> system_framework_dirs;
  CopyIncludeDirs(compiler_info.system_framework_paths(),
                  compiler_info.toolchain_root(), &system_framework_dirs);
  MergeIncludeDirs(current_directory, abs_user_framework_dirs,
                   system_framework_dirs, &framework_dirs);
#else
  CHECK(compiler_info.system_framework_paths().empty());
#endif

  // TODO: cleanup paths (// -> /, /./ -> /) in include_dirs
  // Note that we should not use ResolvePath for these dirs.
  IncludeFileFinder include_file_finder(current_directory, ignore_case,
                                        &include_dirs, &framework_dirs,
                                        file_stat_cache);

  std::vector<std::pair<string, int>> root_includes_with_index =
      CalculateRootIncludesWithIncludeDirIndex(
          root_includes, current_directory, compiler_flags,
          &include_file_finder, include_files);
  root_includes_with_index.emplace_back(PathResolver::PlatformConvert(filename),
                                        CppParser::kCurrentDirIncludeDirIndex);

  IncludePathsObserver include_observer(current_directory,
                                        &cpp_parser_, include_files,
                                        file_stat_cache, &include_file_finder);
  IncludeErrorObserver error_observer;
  cpp_parser_.set_include_observer(&include_observer);
  if (VLOG_IS_ON(1))
    cpp_parser_.set_error_observer(&error_observer);
  cpp_parser_.SetCompilerInfo(&compiler_info);
  if (compiler_flags.type() == CompilerFlagType::Clexe) {
    cpp_parser_.set_is_vc();
  }

  for (const auto& commandline_macro : commandline_macros) {
    const string& macro = commandline_macro.first;
    if (commandline_macro.second) {
      size_t found = macro.find('=');
      if (found == string::npos) {
        // https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html
        // -D name
        //   Predefine name as a macro, with definition 1.
        cpp_parser_.AddMacroByString(macro, "1");
        continue;
      }
      const string& key = macro.substr(0, found);
      const string& value = macro.substr(found + 1, macro.size() - (found + 1));
      cpp_parser_.AddMacroByString(key, value);
    } else {
      cpp_parser_.DeleteMacro(macro);
    }
  }

  // From GCC 4.8, stdc-predef.h is automatically included without
  // -ffreestanding. Also, -fno-hosted is equivalent to -ffreestanding.
  // See also: https://gcc.gnu.org/gcc-4.8/porting_to.html
  if (compiler_flags.type() == CompilerFlagType::Gcc &&
      compiler_info.name().find("clang") == string::npos) {
    const GCCFlags& flags = static_cast<const GCCFlags&>(compiler_flags);
    if (!(flags.has_ffreestanding() || flags.has_fno_hosted())) {
      // TODO: Some environment might not have stdc-predef.h
      // (e.g. android). In that case, IncludeProcess currently emit WARNING,
      // but it's ignoreable. It would be better to suppress such warning.
      const string stdc_predef_input(
          "#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 8)\n"
          "#include <stdc-predef.h>\n"
          "#endif\n");
      cpp_parser_.AddStringInput(stdc_predef_input, "(stdc-predef)");
      if (!cpp_parser_.ProcessDirectives()) {
        LOG(ERROR) << "failed to handle stdc-predef";
      }
      // Since base_file_ will be updated in the last AddStringInput, we need
      // to clear it. Otherwise, test will fail.
      cpp_parser_.ClearBaseFile();
    }
  }

  for (const auto& input_index : root_includes_with_index) {
    const string& input = input_index.first;
    const int dir_index = input_index.second;

    const std::string& abs_input =
        file::JoinPathRespectAbsolute(current_directory, input);
    std::unique_ptr<Content> content(Content::CreateFromFile(abs_input));
    if (!content) {
      LOG(ERROR) << "root include:" << abs_input << " not found";
      return false;
    }

    // TODO: To mitigate b/78094849, let me run directive filter for
    // sources, too.
    SharedCppDirectives directives(CppDirectiveParser::ParseFromContent(
        *DirectiveFilter::MakeFilteredContent(*content), abs_input));
    if (!directives) {
      LOG(ERROR) << "failed to parse directives: " << abs_input;
      return false;
    }
    VLOG(2) << "Looking into " << abs_input;

    string input_basedir = string(file::Dirname(input));

    cpp_parser_.AddFileInput(IncludeItem(std::move(directives), ""), input,
                             input_basedir, dir_index);
    if (!cpp_parser_.ProcessDirectives()) {
      LOG(ERROR) << "cpp parser fatal error in " << abs_input;
      return false;
    }
  }
  return true;
}

std::vector<std::pair<string, int>>
CppIncludeProcessor::CalculateRootIncludesWithIncludeDirIndex(
    const std::vector<string>& root_includes,
    const string& current_directory,
    const CompilerFlags& compiler_flags,
    IncludeFileFinder* include_file_finder,
    std::set<string>* include_files) {
  std::vector<std::pair<string, int>> result;
  for (const auto& root_include : root_includes) {
    string abs_filepath = PathResolver::PlatformConvert(
        file::JoinPathRespectAbsolute(current_directory, root_include));

// TODO: this does not seem to apply to Windows. Need verify.
#ifndef _WIN32
    if (IncludeFileFinder::gch_hack_enabled()) {
      // If there is the precompiled header for this header, we'll send
      // the precompiled header. Note that we don't need to check its content.
      string gch_filepath = abs_filepath + GOMA_GCH_SUFFIX;
      ScopedFd fd(ScopedFd::OpenForRead(gch_filepath));
      if (fd.valid()) {
        fd.Close();
        VLOG(1) << "precompiled header found: " << gch_filepath;
        include_files->insert(root_include + GOMA_GCH_SUFFIX);
        continue;
      }
    }
#endif

    if (access(abs_filepath.c_str(), R_OK) == 0) {
#ifndef _WIN32
      // we don't support *.gch on Win32.
      CopyOriginalFileFromHashCriteria(abs_filepath);
#endif

      // -include can be used twice. So we need to keep it in result
      // if it's duplicated.
      include_files->insert(root_include);
      result.emplace_back(root_include, CppParser::kCurrentDirIncludeDirIndex);
      continue;
    }

    std::string filepath;
    int dir_index = CppParser::kIncludeDirIndexStarting;
    if (!include_file_finder->Lookup(root_include, &filepath, &dir_index)) {
      LOG(INFO) << (compiler_flags.type() == CompilerFlagType::Clexe
                        ? "/FI"
                        : "-include")
                << " not found: " << root_include;
      result.emplace_back(root_include, CppParser::kCurrentDirIncludeDirIndex);
      continue;
    }

    if (IncludeFileFinder::gch_hack_enabled() &&
        absl::EndsWith(filepath, GOMA_GCH_SUFFIX)) {
      VLOG(1) << "precompiled header found: " << filepath;
      include_files->insert(filepath);
      continue;
    }

    // -include can be used twice. So we need to keep it in result
    // if it's duplicated.
    include_files->insert(filepath);
    result.emplace_back(filepath, dir_index);
  }

  return result;
}

int CppIncludeProcessor::total_files() const {
  return cpp_parser_.total_files();
}

int CppIncludeProcessor::skipped_files() const {
  return cpp_parser_.skipped_files();
}

}  // namespace devtools_goma
