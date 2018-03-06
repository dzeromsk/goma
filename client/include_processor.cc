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
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "autolock_timer.h"
#include "compiler_flags.h"
#include "compiler_info.h"
#include "content.h"
#include "counterz.h"
#include "cpp_parser.h"
#include "directive_filter.h"
#include "env_flags.h"
#include "file.h"
#include "file_dir.h"
#include "flag_parser.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "glog/vlog_is_on.h"
#include "goma_init.h"
#include "include_cache.h"
#include "include_file_finder.h"
#include "include_file_utils.h"
#include "include_processor.h"
#include "ioutil.h"
#include "lockhelper.h"
#include "path.h"
#include "path_resolver.h"
#include "scoped_fd.h"
#include "util.h"

#ifdef _WIN32
#include "path_resolver.h"
#include "posix_helper_win.h"
#endif

#if HAVE_CPU_PROFILER
#include <gperftools/profiler.h>
#endif

using std::string;

namespace devtools_goma {

namespace {

// Reads content from |filepath| and set |next_current_directory|.
// If |file_id_cache| has a FileId for |filepath|, we use it.
// Otherwise we take FileId for |filepath| and stores it to |file_id_cache|.
// We don't take |file_id_cache| ownership.
std::pair<std::unique_ptr<Content>, FileId> TryInclude(
    const string& cwd, const string& filepath, string* next_current_directory,
    FileIdCache* file_id_cache) {
  GOMA_COUNTERZ("TryInclude");

  const string abs_filepath = file::JoinPathRespectAbsolute(cwd, filepath);
  FileId file_id(file_id_cache->Get(abs_filepath));
  if (!file_id.IsValid()) {
    return {nullptr, FileId()};
  }

  if (file_id.is_directory) {
    VLOG(2) << "TryInclude but dir:" << abs_filepath;
    return {nullptr, FileId()};
  }

  std::unique_ptr<Content> fp;
  if (IncludeCache::IsEnabled()) {
    // When IncludeCache is enabled and the file is not updated,
    // we load a minified header from memory.

    fp = IncludeCache::instance()->GetCopyIfNotModified(abs_filepath, file_id);
    if (!fp) {
      // TODO: If we can use shared_ptr for Content, we would be able
      // to omit copying from IncludeCache. In that case, we should return
      // something like shared_ptr<const Content> from this function.

      ScopedFd fd(ScopedFd::OpenForRead(abs_filepath));
      if (!fd.valid())
        return {nullptr, FileId()};

      fp = Content::CreateFromFileDescriptor(abs_filepath, fd, file_id.size);
      if (!fp)
        return {nullptr, FileId()};
      fp = IncludeCache::instance()->Insert(abs_filepath, *fp, file_id);
    }
  } else {
    ScopedFd fd(ScopedFd::OpenForRead(abs_filepath));
    if (!fd.valid())
      return {nullptr, FileId()};

    fp = Content::CreateFromFileDescriptor(abs_filepath, fd, file_id.size);
    if (!fp)
      return {nullptr, FileId()};
    fp = DirectiveFilter::MakeFilteredContent(*fp);
  }

  GetBaseDir(filepath, next_current_directory);
  return {std::move(fp), file_id};
}

}  // anonymous namespace

class IncludePathsObserver : public CppParser::IncludeObserver {
 public:
  IncludePathsObserver(
      const std::string& cwd,
      bool ignore_case,
      CppParser* parser,
      std::set<string>* shared_include_files,
      FileIdCache* file_id_cache,
      IncludeFileFinder* include_file_finder)
      : cwd_(cwd), ignore_case_(ignore_case), parser_(parser),
        shared_include_files_(shared_include_files),
        file_id_cache_(file_id_cache),
        include_file_finder_(include_file_finder) {
    CHECK(parser_);
    CHECK(shared_include_files_);
    CHECK(file_id_cache_);
  }

  bool HandleInclude(
      const string& path,
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

    VLOG(2) << current_filepath << ": including "
            << quote_char << path
            << " dir:" << current_directory
            << " include_dir_index:" << include_dir_index;

    string next_current_directory;
    string filepath;

    if (quote_char == '"') {
      // Look for current directory.
      if (HandleIncludeInDir(current_directory, path,
                             include_dir_index,
                             &next_current_directory)) {
        return true;
      }

      // If not found in current directory, try all include paths.
      include_dir_index = CppParser::kIncludeDirIndexStarting;
    }

    // Look for include dirs from |include_dir_index|.
    int dir_index = include_dir_index;
    if (!include_file_finder_->Lookup(path, &filepath, &dir_index) &&
        !include_file_finder_->LookupSubframework(
            path, current_directory, &filepath)) {
      VLOG(2) << "Not found: " << path;
      return false;
    }

    VLOG(3) << "Lookup => " << filepath << " dir_index=" << dir_index;

    if (parser_->IsProcessedFile(filepath, include_dir_index)) {
      VLOG(2) << "Already processed:" << quote_char << filepath;
      return true;
    }

    auto file_content = TryInclude(
        cwd_, filepath, &next_current_directory, file_id_cache_);
    std::unique_ptr<Content> next_fp = std::move(file_content.first);

    if (next_fp.get()) {
      if (IncludeFileFinder::gch_hack_enabled() &&
          absl::EndsWith(filepath, GOMA_GCH_SUFFIX) &&
          !absl::EndsWith(path, GOMA_GCH_SUFFIX)) {
        VLOG(2) << "Found a precompiled header: " << filepath;
        shared_include_files_->insert(filepath);
        return true;
      }

      VLOG(2) << "Looking into " << filepath << " index=" << dir_index;
      shared_include_files_->insert(filepath);
      parser_->AddFileInput(std::move(next_fp), file_content.second, filepath,
                            next_current_directory, dir_index);
      return true;
    }
    VLOG(2) << "include file not found in dir_cache?";
    return false;
  }

  bool HasInclude(
      const string& path,
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
    const std::string abs_filepath = file::JoinPathRespectAbsolute(
        cwd_, filepath);
    if (shared_include_files_->count(filepath) ||
        access(abs_filepath.c_str(), R_OK) == 0) {
      DCHECK(!file::IsDirectory(abs_filepath, file::Defaults()).ok())
          << abs_filepath;
      return true;
    }
    return false;
  }

 private:
  bool CanPruneWithTopPathComponent(const string& dir, const string& path) {
    const std::string& dir_with_top_path_component = file::JoinPath(
        dir, IncludeFileFinder::TopPathComponent(path, ignore_case_));
    return !file_id_cache_->Get(dir_with_top_path_component).IsValid();
  }

  bool HandleIncludeInDir(const string& dir, const string& path,
                          int include_dir_index,
                          string* next_current_directory) {
    GOMA_COUNTERZ("handle include try");
    if (CanPruneWithTopPathComponent(
            file::JoinPathRespectAbsolute(cwd_, dir), path)) {
      GOMA_COUNTERZ("handle include pruned");
      return false;
    }

    string filepath = PathResolver::PlatformConvert(
        file::JoinPathRespectAbsolute(dir, path));

    if (IncludeFileFinder::gch_hack_enabled()) {
      const string& gchpath = filepath + GOMA_GCH_SUFFIX;
      std::unique_ptr<Content> fp =
          TryInclude(cwd_, gchpath, next_current_directory,
                     file_id_cache_).first;
      if (fp) {
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
    auto file_content = TryInclude(cwd_, filepath, next_current_directory,
                                   file_id_cache_);
    std::unique_ptr<Content> fp = std::move(file_content.first);
    FileId fileid = file_content.second;
    if (fp) {
      shared_include_files_->insert(filepath);
      parser_->AddFileInput(std::move(fp), fileid, filepath,
                            *next_current_directory, include_dir_index);
      return true;
    }
    VLOG(2) << "include file not found in current directoy? filepath="
            << filepath;
    return false;
  }

  bool HasIncludeInDir(const string& dir, const string& path,
                       const string& current_filepath) {
    const std::string& filepath = file::JoinPathRespectAbsolute(dir, path);
    string abs_filepath = file::JoinPathRespectAbsolute(cwd_, filepath);
    string abs_current_filepath = file::JoinPathRespectAbsolute(
        cwd_, current_filepath);
    abs_filepath = PathResolver::ResolvePath(abs_filepath);
    bool is_current = (abs_filepath == abs_current_filepath);
    if (is_current)
      return true;
    if (!file::IsDirectory(abs_filepath, file::Defaults()).ok() &&
        (shared_include_files_->count(filepath) ||
         access(abs_filepath.c_str(), R_OK) == 0 ||
         (IncludeFileFinder::gch_hack_enabled() &&
          access((abs_filepath + GOMA_GCH_SUFFIX).c_str(), R_OK) == 0))) {
      return true;
    }
    return false;
  }

  const std::string cwd_;
  const bool ignore_case_;
  CppParser* parser_;
  std::set<string>* shared_include_files_;
  FileIdCache* file_id_cache_;

  IncludeFileFinder* include_file_finder_;

  DISALLOW_COPY_AND_ASSIGN(IncludePathsObserver);
};

class IncludeErrorObserver : public CppParser::ErrorObserver {
 public:
  IncludeErrorObserver() {}

  void HandleError(const string& error) override {
    // Note that we don't set this error observer if VLOG_IS_ON(1) is false.
    // If you need to change this code, make sure you'll modify
    // set_error_observer call in IncludeProcessor::GetIncludeFiles()
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
  File::Copy(original_filepath.c_str(), tmp_filepath.c_str(), true);
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

static void MergeDirs(
    const std::string cwd,
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

static void MergeIncludeDirs(
    const std::string& cwd,
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

  MergeDirs(cwd, nonsystem_include_dirs, include_dirs,
            &seen_include_dir_set);

  copy(unique_system_include_dirs.begin(), unique_system_include_dirs.end(),
       back_inserter(*include_dirs));
}

bool IncludeProcessor::GetIncludeFiles(
    const string& filename,
    const string& current_directory,
    const CompilerFlags& compiler_flags,
    const CompilerInfo& compiler_info,
    std::set<string>* include_files,
    FileIdCache* file_id_cache) {
  DCHECK(!current_directory.empty());
  DCHECK(file::IsAbsolutePath(current_directory)) << current_directory;

  delayed_macro_includes_.clear();

  std::vector<string> non_system_include_dirs;
  std::vector<string> root_includes;
  std::vector<string> user_framework_dirs;
  std::vector<std::pair<string, bool>> commandline_macros;
#if _WIN32
  bool ignore_case = true;
#else
  bool ignore_case = false;
#endif

  if (compiler_flags.is_gcc()) {
    const GCCFlags& flags = static_cast<const GCCFlags&>(compiler_flags);
    non_system_include_dirs = flags.non_system_include_dirs();
    root_includes = flags.root_includes();
    user_framework_dirs = flags.framework_dirs();
    commandline_macros = flags.commandline_macros();
  } else if (compiler_flags.is_vc()) {
    const VCFlags& flags = static_cast<const VCFlags&>(compiler_flags);
    non_system_include_dirs = flags.include_dirs();
    root_includes = flags.root_includes();
    commandline_macros = flags.commandline_macros();
    ignore_case = true;
  } else if (compiler_flags.is_clang_tidy()) {
    const ClangTidyFlags& flags =
        static_cast<const ClangTidyFlags&>(compiler_flags);
    non_system_include_dirs = flags.non_system_include_dirs();
    root_includes = flags.root_includes();
    user_framework_dirs = flags.framework_dirs();
    commandline_macros = flags.commandline_macros();
  } else {
    LOG(FATAL) << "Bad compiler_flags for IncludeProcessor: "
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
  CopyIncludeDirs(
      compiler_info.quote_include_paths(),
      "",
      &quote_dirs);

  std::vector<string> all_system_include_dirs;
  if (compiler_info.lang().find("c++") != string::npos) {
    CopyIncludeDirs(
        compiler_info.cxx_system_include_paths(),
        compiler_info.toolchain_root(),
        &all_system_include_dirs);
  } else {
    CopyIncludeDirs(
        compiler_info.system_include_paths(),
        compiler_info.toolchain_root(),
        &all_system_include_dirs);
  }

  // The first element of include_dirs.include_dirs represents the current input
  // directory. It's not specified by -I, but we need to handle it when
  // including file with #include "".
  std::vector<std::string> include_dirs;
  std::vector<std::string> framework_dirs;
  include_dirs.push_back(current_directory);
  copy(quote_dirs.begin(), quote_dirs.end(),
       back_inserter(include_dirs));

  cpp_parser_.set_bracket_include_dir_index(
      include_dirs.size());
  VLOG(2) << "bracket include dir index=" <<
      include_dirs.size();
  MergeIncludeDirs(current_directory,
                   non_system_include_dirs,
                   all_system_include_dirs,
                   &include_dirs);

#ifndef _WIN32
  std::vector<string> abs_user_framework_dirs;
  CopyIncludeDirs(
      user_framework_dirs,
      "",
      &abs_user_framework_dirs);
  std::vector<string> system_framework_dirs;
  CopyIncludeDirs(
      compiler_info.system_framework_paths(),
      compiler_info.toolchain_root(),
      &system_framework_dirs);
  MergeIncludeDirs(current_directory,
                   abs_user_framework_dirs,
                   system_framework_dirs,
                   &framework_dirs);
#else
  CHECK(compiler_info.system_framework_paths().empty());
#endif

  // TODO: cleanup paths (// -> /, /./ -> /) in include_dirs
  // Note that we should not use ResolvePath for these dirs.
  IncludeFileFinder include_file_finder(
      current_directory, ignore_case, &include_dirs, &framework_dirs,
      file_id_cache);

  for (size_t i = 0; i < root_includes.size();) {
    string abs_filepath =
        PathResolver::PlatformConvert(
            file::JoinPathRespectAbsolute(current_directory, root_includes[i]));

    // TODO: this does not seem to apply to Windows. Need verify.
#ifndef _WIN32
    if (IncludeFileFinder::gch_hack_enabled()) {
      // If there is the precompiled header for this header, we'll send
      // the precompiled header. Note that we don't need to check its content.
      const string& gch_filepath = abs_filepath + GOMA_GCH_SUFFIX;
      {
        ScopedFd fd(ScopedFd::OpenForRead(gch_filepath));
        if (fd.valid()) {
          fd.Close();
          VLOG(1) << "precompiled header found: " << gch_filepath;
          include_files->insert(root_includes[i] + GOMA_GCH_SUFFIX);
          root_includes.erase(root_includes.begin() + i);
          continue;
        }
      }
    }
#endif

    if (access(abs_filepath.c_str(), R_OK) == 0) {
#ifndef _WIN32
      // we don't support *.gch on Win32.
      CopyOriginalFileFromHashCriteria(abs_filepath);
#endif

      if (include_files->insert(root_includes[i]).second) {
        i++;
      } else {
        root_includes.erase(root_includes.begin() + i);
      }
      continue;
    }

    std::string filepath;
    {
      int dir_index = CppParser::kIncludeDirIndexStarting;
      if (!include_file_finder.Lookup(root_includes[i],
                                      &filepath,
                                      &dir_index)) {
        LOG(INFO) << (compiler_flags.is_vc() ? "/FI" : "-include")
                  << " not found: " << root_includes[i];
        i++;
        continue;
      }
    }

    if (IncludeFileFinder::gch_hack_enabled() &&
        absl::EndsWith(filepath, GOMA_GCH_SUFFIX)) {
      VLOG(1) << "precompiled header found: " << filepath;
      include_files->insert(filepath);
      root_includes.erase(root_includes.begin() + i);
      continue;
    }

    if (include_files->insert(filepath).second) {
      root_includes[i] = filepath;
      i++;
    } else {
      root_includes.erase(root_includes.begin() + i);
    }
  }

  root_includes.push_back(PathResolver::PlatformConvert(filename));

  IncludePathsObserver include_observer(
      current_directory,
      ignore_case,
      &cpp_parser_,
      include_files, file_id_cache,
      &include_file_finder);
  IncludeErrorObserver error_observer;
  cpp_parser_.set_include_observer(&include_observer);
  if (VLOG_IS_ON(1))
    cpp_parser_.set_error_observer(&error_observer);
  cpp_parser_.SetCompilerInfo(&compiler_info);
  if (compiler_flags.is_vc()) {
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
  if (compiler_flags.is_gcc() &&
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

  for (const auto& input : root_includes) {
    const std::string& abs_input = file::JoinPathRespectAbsolute(
        current_directory, input);
    std::unique_ptr<Content> fp(Content::CreateFromFile(abs_input));
    if (!fp) {
      LOG(ERROR) << "root include:" << abs_input << " not found";
      return false;
    }
    VLOG(2) << "Looking into " << abs_input;

    string input_basedir;
    GetBaseDir(input, &input_basedir);

    cpp_parser_.AddFileInput(std::move(fp), file_id_cache->Get(abs_input),
                             input, input_basedir,
                             CppParser::kCurrentDirIncludeDirIndex);
    if (!cpp_parser_.ProcessDirectives()) {
      LOG(ERROR) << "cpp parser fatal error in " << abs_input;
      return false;
    }
  }
  return true;
}

int IncludeProcessor::total_files() const {
  return cpp_parser_.total_files();
}

int IncludeProcessor::skipped_files() const {
  return cpp_parser_.skipped_files();
}

}  // namespace devtools_goma

#ifdef TEST
#ifndef _WIN32
#include <unistd.h>
#endif
#include <time.h>
#include "file_helper.h"
#include "scoped_tmp_file.h"
#include "subprocess.h"

// TODO: share this code with include_processor_unittest.
std::set<string> GetExpectedFiles(const std::vector<string>& args,
                                  const std::vector<string>& env,
                                  const string& cwd) {
  std::set<string> expected_files;
#ifndef _WIN32
  // TODO: ReadCommandOutputByPopen couldn't read large outputs
  // and causes exit=512, so use tmpfile.
  devtools_goma::ScopedTmpFile tmpfile("include_processor_verify");
  tmpfile.Close();
  std::vector<string> run_args;
  for (size_t i = 0; i < args.size(); ++i) {
    const string& arg = args[i];
    if (strncmp(arg.c_str(), "-M", 2) == 0) {
      if (arg == "-MF" || arg == "-MT" || arg == "-MQ") {
        ++i;
      }
      continue;
    }
    if (arg == "-o") {
      ++i;
      continue;
    }
    if (strncmp(arg.c_str(), "-o", 2) == 0) {
      continue;
    }
    run_args.push_back(arg);
  }
  run_args.push_back("-M");
  run_args.push_back("-MF");
  run_args.push_back(tmpfile.filename());

  std::vector<string> run_env(env);
  run_env.push_back("LC_ALL=C");

  // The output format of -M will be
  //
  // stdio: /usr/include/stdio.h /usr/include/features.h \\\n
  //   /usr/include/sys/cdefs.h /usr/include/bits/wordsize.h \\\n
  //   ...
  int status;
  devtools_goma::ReadCommandOutputByPopen(
      run_args[0], run_args, run_env,
      cwd, devtools_goma::MERGE_STDOUT_STDERR, &status);
  if (status != 0) {
    LOG(INFO) << "args:" << run_args;
    LOG(INFO) << "env:" << run_env;
    LOG(FATAL) << "status:" << status;
  }
  string output;
  CHECK(devtools_goma::ReadFileToString(tmpfile.filename(), &output));
  std::vector<string> files = ToVector(
      absl::StrSplit(output, absl::ByAnyChar(" \n\r\\"), absl::SkipEmpty()));
  devtools_goma::PathResolver pr;
  // Skip the first element as it's the make target.
  for (size_t i = 1; i < files.size(); i++) {
    const string& file = files[i];
    // Need normalization as GCC may output a same file in different way.
    // TODO: don't use ResolvePath.
    expected_files.insert(pr.ResolvePath(
        file::JoinPathRespectAbsolute(cwd, file)));
  }
#endif
  return expected_files;
}

std::set<string> NormalizePaths(
    const string& cwd, const std::set<string>& paths) {
  std::set<string> normalized;
  for (const auto& iter : paths) {
    normalized.insert(devtools_goma::PathResolver::ResolvePath(
        file::JoinPathRespectAbsolute(cwd, iter)));
  }
  return normalized;
}

int CompareFiles(const std::set<string>& expected_files,
                 const std::set<string>& actual_files) {
  std::vector<string> matched;
  std::vector<string> extra;
  std::vector<string> missing;
  std::set_intersection(expected_files.begin(), expected_files.end(),
                        actual_files.begin(), actual_files.end(),
                        back_inserter(matched));

  std::set_difference(actual_files.begin(), actual_files.end(),
                      expected_files.begin(), expected_files.end(),
                      back_inserter(extra));

  std::set_difference(expected_files.begin(), expected_files.end(),
                      actual_files.begin(), actual_files.end(),
                      back_inserter(missing));

  for (const auto& extra_iter : extra) {
    LOG(INFO) << "Extra include:" << extra_iter;
  }
  for (const auto& missing_iter : missing) {
    LOG(ERROR) << "Missing include:" << missing_iter;
  }

  LOG(INFO) << "matched:" << matched.size()
            << " extra:" << extra.size()
            << " missing:" << missing.size();

  return missing.size();
}

void GetAdditionalEnv(
    const char** envp, const char* name, std::vector<string>* envs) {
  int namelen = strlen(name);
  for (const char** e = envp; *e; e++) {
    if (
#ifdef _WIN32
            _strnicmp(*e, name, namelen) == 0
#else
            strncmp(*e, name, namelen) == 0
#endif
            && (*e)[namelen] == '=') {
      envs->push_back(*e);
      return;
    }
  }
}

int main(int argc, char *argv[], const char** envp) {
  devtools_goma::Init(argc, argv, envp);
  devtools_goma::InitLogging(argv[0]);

  bool verify_mode = false;
  if (argc >= 2 && !strcmp(argv[1], "--verify")) {
    verify_mode = true;
    argc--;
    argv++;
#ifdef _WIN32
    std::cerr << "--verify is not yet supported on win32" << std::endl;
    exit(1);
#endif
  }

  int loop_count = 1;
  if (argc >= 2 && absl::StartsWith(argv[1], "--count=")) {
    loop_count = atoi(argv[1] + 8);
    argc--;
    argv++;

    std::cerr << "Run IncludeProcessor::GetIncludeFiles "
              << loop_count << " times." << std::endl;
  }

#ifndef _WIN32
  if (argc == 1) {
    std::cerr << argv[0] << " [full path of local compiler [args]]"
              << std::endl;
    std::cerr << "e.g.: " << argv[0] << " /usr/bin/gcc -c tmp.c" << std::endl;
    exit(1);
  }
  if (argv[1][0] != '/') {
    std::cerr << "argv[1] is not absolute path for local compiler."
              << std::endl;
    exit(1);
  }

  devtools_goma::InstallReadCommandOutputFunc(
      devtools_goma::ReadCommandOutputByPopen);
#else
  if (argc == 1) {
    std::cerr << argv[0] << " [full path of local compiler [args]]"
              << std::endl;
    std::cerr << "e.g.: " << argv[0] << " C:\\vs\\vc\\bin\\cl.exe /c c1.c"
              << std::endl;
    std::cerr << "Compiler path must be absolute path." << std::endl;
    exit(1);
  }

  devtools_goma::InstallReadCommandOutputFunc(
      devtools_goma::ReadCommandOutputByRedirector);
#endif

  devtools_goma::IncludeFileFinder::Init(false);

  const string cwd = devtools_goma::GetCurrentDirNameOrDie();
  std::vector<string> args;
  for (int i = 1; i < argc; i++)
    args.push_back(argv[i]);

  std::unique_ptr<devtools_goma::CompilerFlags> flags(
      devtools_goma::CompilerFlags::MustNew(args, cwd));
  std::vector<string> compiler_info_envs;
  flags->GetClientImportantEnvs(envp, &compiler_info_envs);

  // These env variables are needed to run cl.exe
  GetAdditionalEnv(envp, "PATH", &compiler_info_envs);
  GetAdditionalEnv(envp, "TMP", &compiler_info_envs);
  GetAdditionalEnv(envp, "TEMP", &compiler_info_envs);

  devtools_goma::CompilerInfoBuilder cib;
  std::unique_ptr<devtools_goma::CompilerInfoData> cid(
      cib.FillFromCompilerOutputs(
          *flags, args[0], compiler_info_envs));
  devtools_goma::CompilerInfo compiler_info(std::move(cid));
  if (compiler_info.HasError()) {
    std::cerr << compiler_info.error_message() << std::endl;
    exit(1);
  }

  std::set<string> include_files;

#if HAVE_CPU_PROFILER
  ProfilerStart(file::JoinPathRespectAbsolute(
      FLAGS_TMP_DIR, FLAGS_INCLUDE_PROCESSOR_CPU_PROFILE_FILE).c_str());
#endif

  for (int i = 0; i < loop_count; ++i) {
    devtools_goma::IncludeProcessor include_processor;
    devtools_goma::FileIdCache file_id_cache;
    include_files.clear();

    clock_t start_time = clock();
    for (const auto& iter : flags->input_filenames()) {
      bool ok = include_processor.GetIncludeFiles(
          iter,
          cwd,
          *flags,
          compiler_info,
          &include_files,
          &file_id_cache);
      if (!ok) {
        std::cerr << "GetIncludeFiles failed" << std::endl;
        exit(1);
      }
    }
    clock_t end_time = clock();

    // Show the result only for the first time.
    if (i == 0) {
      for (const auto& iter : include_files) {
        std::cout << iter << std::endl;
      }
      std::cerr << "listed/skipped/total files: "
                << include_files.size() << " / "
                << include_processor.cpp_parser()->skipped_files() << " / "
                << include_processor.cpp_parser()->total_files() << std::endl;
    }

    if (loop_count != 1) {
      std::cerr << "Run " << i << ": ";
    }
    std::cerr << (end_time - start_time) * 1000.0 / CLOCKS_PER_SEC << "msec"
              << std::endl;
  }

#if HAVE_CPU_PROFILER
  ProfilerStop();
#endif

  if (verify_mode) {
    for (const auto& iter : flags->input_filenames()) {
      include_files.insert(file::JoinPathRespectAbsolute(cwd, iter));
    }
    std::set<string> actual = NormalizePaths(cwd, include_files);
    std::set<string> expected = GetExpectedFiles(args, compiler_info_envs, cwd);
    std::cout << "expected" << std::endl;
    for (const auto& iter : expected) {
      std::cout << iter << std::endl;
    }
    std::cout << "compare" << std::endl;
    int missings = CompareFiles(expected, actual);
    if (missings > 0) {
      LOG(ERROR) << "missing files:" << missings;
      exit(1);
    }
  }
}
#endif
