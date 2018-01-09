// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_info.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "autolock_timer.h"
#include "cmdline_parser.h"
#include "compiler_flags.h"
#include "compiler_specific.h"
#include "file_dir.h"
#include "file.h"
#include "file_id.h"
#include "flag_parser.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "goma_hash.h"
#include "ioutil.h"
#include "mypath.h"
#include "path.h"
#include "path_resolver.h"
#include "path_util.h"
#include "scoped_tmp_file.h"
#include "split.h"
#include "string_piece_utils.h"
#include "util.h"

#ifdef _WIN32
#include "config_win.h"
#include "posix_helper_win.h"
#endif

namespace devtools_goma {

namespace {

#include "clang_features.cc"

void SetFileIdToData(const FileId& file_id, CompilerInfoData::FileId* data) {
#ifdef _WIN32
  data->set_volume_serial_number(file_id.volume_serial_number);
  data->set_file_index_high(file_id.file_index_high);
  data->set_file_index_low(file_id.file_index_low);
#else
  data->set_dev(file_id.dev);
  data->set_inode(file_id.inode);
#endif
  data->set_mtime(file_id.mtime);
  data->set_size(file_id.size);
  data->set_is_directory(file_id.is_directory);
}

void GetFileIdFromData(const CompilerInfoData::FileId& data,
                       FileId* file_id) {
#ifdef _WIN32
  file_id->volume_serial_number = data.volume_serial_number();
  file_id->file_index_high = data.file_index_high();
  file_id->file_index_low = data.file_index_low();
#else
  file_id->dev = data.dev();
  file_id->inode = data.inode();
#endif
  file_id->mtime = data.mtime();
  file_id->size = data.size();
  file_id->is_directory = data.is_directory();
}

// If |path| exsts in |sha256_cache|, the value is returned.
// Otherwise, calculate sha256 hash from |path|, and put the result
// to |sha256_cache|.
// Returns false if calculating sha256 hash from |path| failed.
bool GetHashFromCacheOrFile(const string& path,
                            string* hash,
                            unordered_map<string, string>* sha256_cache) {
  auto it = sha256_cache->find(path);
  if (it != sha256_cache->end()) {
    *hash = it->second;
    return true;
  }

  if (!GomaSha256FromFile(path, hash))
    return false;

  sha256_cache->insert(make_pair(path, *hash));
  return true;
}

bool AddSubprogramInfo(
    const string& path,
    google::protobuf::RepeatedPtrField<CompilerInfoData::SubprogramInfo>* ss) {
  CompilerInfoData::SubprogramInfo* s = ss->Add();
  if (!CompilerInfoBuilder::SubprogramInfoFromPath(path, s)) {
    ss->RemoveLast();
    return false;
  }
  return true;
}

}  // anonymous namespace.

#ifdef _WIN32
// GetNaClToolchainRoot is a part of hack needed for
// the (build: Windows, target: NaCl) compile.
static string GetNaClToolchainRoot(const string& normal_nacl_gcc_path) {
  return PathResolver::ResolvePath(
      file::JoinPath(file::Dirname(normal_nacl_gcc_path), ".."));
}
#endif

// Execute GCC and get the string output for GCC version
static bool GetGccVersion(const string& bare_gcc,
                          const std::vector<string>& compiler_info_envs,
                          const string& cwd,
                          string* version) {
  std::vector<string> argv;
  argv.push_back(bare_gcc);
  argv.push_back("-dumpversion");
  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");
  int32_t status = 0;
  const string dumpversion_output(
      ReadCommandOutput(bare_gcc, argv, env, cwd,
                        MERGE_STDOUT_STDERR, &status));
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " bare_gcc=" << bare_gcc
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " dumpversion_output=" << dumpversion_output;
    return false;
  }

  argv[1] = "--version";
  const string version_output(
      ReadCommandOutput(bare_gcc, argv, env, cwd,
                        MERGE_STDOUT_STDERR, &status));
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " bare_gcc=" << bare_gcc
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " version_output=" << version_output;
    return false;
  }

  if (dumpversion_output.empty() || version_output.empty()) {
    LOG(ERROR) << "dumpversion_output or version_output is empty."
               << " bare_gcc=" << bare_gcc
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " dumpversion_output=" << dumpversion_output
               << " version_output=" << version_output;
    return false;
  }
  *version = GetCxxCompilerVersionFromCommandOutputs(bare_gcc,
                                                     dumpversion_output,
                                                     version_output);
  return true;
}

// Execute GCC and get the string output for GCC target architecture
// This target is used to pick the same compiler in the backends, so
// we don't need to use compiler_info_flags here.
static bool GetGccTarget(const string& bare_gcc,
                         const std::vector<string>& compiler_info_envs,
                         const string& cwd,
                         string* target) {
  std::vector<string> argv;
  argv.push_back(bare_gcc);
  argv.push_back("-dumpmachine");
  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");
  int32_t status = 0;
  string gcc_output(ReadCommandOutput(bare_gcc, argv, env, cwd,
                                      MERGE_STDOUT_STDERR, &status));
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " bare_gcc=" << bare_gcc
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " gcc_output=" << gcc_output;
    return false;
  }
  *target = GetFirstLine(gcc_output);
  return !target->empty();
}

static bool ParseDriverArgs(const string& display_output,
                            std::vector<string>* driver_args) {
  StringPiece buf(display_output);
  size_t pos;
  do {
    pos = buf.find_first_of("\n");
    StringPiece line = buf.substr(0, pos);
    buf.remove_prefix(pos + 1);
    if (line[0] == ' ') {
      return ParsePosixCommandLineToArgv(string(line), driver_args);
    }
  } while (pos != StringPiece::npos);
  return false;
}


static string GetVCOutputString(const string& cl_exe_path,
                                const string& vcflags,
                                const string& dumb_file,
                                const std::vector<string>& compiler_info_flags,
                                const std::vector<string>& compiler_info_envs,
                                const string& cwd) {
  // The trick we do here gives both include path and predefined macros.
  std::vector<string> argv;
  argv.push_back(cl_exe_path);
  argv.push_back("/nologo");
  argv.push_back(vcflags);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back(dumb_file);
  int32_t dummy;  // It is fine to return non zero status code.
  return ReadCommandOutput(cl_exe_path, argv, compiler_info_envs, cwd,
                           MERGE_STDOUT_STDERR, &dummy);
}

// Since clang-cl is emulation of cl.exe, it might not have meaningful
// clang-cl -dumpversion.  It leads inconsistency of goma's compiler version
// format between clang and clang-cl.  Former expect <dumpversion>[<version>]
// latter cannot have <dumpversion>.
// As a result, let me use different way of getting version string.
// TODO: make this support gcc and use this instead of
//                    GetGccTarget.
static string GetClangClSharpOutput(
    const string& clang_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd) {
  std::vector<string> argv;
  argv.push_back(clang_path);
  copy(compiler_info_flags.begin(),
       compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back("-###");
  int32_t status = 0;
  string output = ReadCommandOutput(
      clang_path, argv, compiler_info_envs, cwd,
      MERGE_STDOUT_STDERR, &status);
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " clang_path=" << clang_path
               << " status=" << status
               << " argv=" << argv
               << " compiler_info_envs=" << compiler_info_envs
               << " cwd=" << cwd
               << " output=" << output;
    return "";
  }
  return output;
}

/* static */
std::unique_ptr<CompilerInfoData> CompilerInfoBuilder::FillFromCompilerOutputs(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  std::unique_ptr<CompilerInfoData> data(new CompilerInfoData);

  data->set_last_used_at(time(nullptr));

  // TODO: minimize the execution of ReadCommandOutput.
  // If we execute gcc/clang with -xc -v for example, we can get not only
  // real compiler path but also target and version.
  // However, I understand we need large refactoring of CompilerInfo
  // for minimizing the execution while keeping readability.
  if (flags.is_gcc()) {
    data->set_local_compiler_path(local_compiler_path);
    data->set_real_compiler_path(
        GetRealCompilerPath(local_compiler_path, flags.cwd(),
                            compiler_info_envs));
  } else {
    data->set_local_compiler_path(local_compiler_path);
    data->set_real_compiler_path(local_compiler_path);
  }

  if (!file::IsAbsolutePath(local_compiler_path)) {
    data->set_cwd(flags.cwd());
  }

  const string& abs_local_compiler_path =
      PathResolver::ResolvePath(
          file::JoinPathRespectAbsolute(flags.cwd(),
                                        data->local_compiler_path()));
  VLOG(2) << "FillFromCompilerOutputs:"
          << " abs_local_compiler_path=" << abs_local_compiler_path
          << " cwd=" << flags.cwd()
          << " local_compiler_path=" << data->local_compiler_path();
  data->set_real_compiler_path(
      PathResolver::ResolvePath(
          file::JoinPathRespectAbsolute(flags.cwd(),
                                        data->real_compiler_path())));

  if (!GomaSha256FromFile(abs_local_compiler_path,
                          data->mutable_local_compiler_hash())) {
    LOG(ERROR) << "Could not open local compiler file "
               << abs_local_compiler_path;
    data->set_found(false);
    return data;
  }

  if (!GomaSha256FromFile(data->real_compiler_path(),
                          data->mutable_hash())) {
    LOG(ERROR) << "Could not open real compiler file "
               << data->real_compiler_path();
    data->set_found(false);
    return data;
  }
  data->set_name(GetCompilerName(*data));
  if (data->name().empty()) {
    AddErrorMessage("Failed to get compiler name of " +
                    abs_local_compiler_path,
                    data.get());
    LOG(ERROR) << data->error_message();
    return data;
  }
  data->set_lang(flags.lang());

  FileId local_compiler_id(abs_local_compiler_path);
  if (!local_compiler_id.IsValid()) {
    LOG(ERROR) << "Failed to get file id of " << abs_local_compiler_path;
    data->set_found(false);
    return data;
  }
  SetFileIdToData(local_compiler_id, data->mutable_local_compiler_id());
  data->mutable_real_compiler_id()->CopyFrom(data->local_compiler_id());

  data->set_found(true);

  if (abs_local_compiler_path != data->real_compiler_path()) {
    FileId real_compiler_id(data->real_compiler_path());
    if (!real_compiler_id.IsValid()) {
      LOG(ERROR) << "Failed to get file id of " << data->real_compiler_path();
      data->set_found(false);
      return data;
    }
    SetFileIdToData(real_compiler_id, data->mutable_real_compiler_id());
  }
  if (flags.is_gcc()) {
    // Some compilers uses wrapper script to set build target, and in such a
    // situation, build target could be different.
    // To make goma backend use proper wrapper script, or set proper -target,
    // we should need to use local_compiler_path instead of real path.
    bool has_version = GetGccVersion(
        abs_local_compiler_path, compiler_info_envs,
        flags.cwd(), data->mutable_version());
    bool has_target = GetGccTarget(
        abs_local_compiler_path, compiler_info_envs,
        flags.cwd(), data->mutable_target());

    bool is_clang = CompilerFlags::IsClangCommand(
        data->real_compiler_path());

    const GCCFlags& gcc_flags = static_cast<const GCCFlags&>(flags);
    const bool is_clang_tidy = false;

    // TODO: As we have -x flags in compiler_info,
    //               include_processor don't need to have 2 kinds of
    //               system include paths (C and C++).
    //               However, we still need them because backend
    //               should set them using different ways
    //               (-isystem and CPLUS_INCLUDE_PATH).
    //               Once b/5218687 is fixed, we should
    //               be able to eliminate cxx_system_include_paths.
    if (!SetBasicCompilerInfo(local_compiler_path,
                              gcc_flags.compiler_info_flags(),
                              compiler_info_envs,
                              gcc_flags.cwd(),
                              "-x" + flags.lang(),
                              gcc_flags.is_cplusplus(),
                              is_clang,
                              is_clang_tidy,
                              gcc_flags.has_nostdinc(),
                              data.get())) {
      DCHECK(data->has_error_message());
      // If error occurred in SetBasicCompilerInfo, we do not need to
      // continue.
      return data;
    }

    if (!has_version) {
      AddErrorMessage("Failed to get version for " +
                      data->real_compiler_path(),
                      data.get());
      LOG(ERROR) << data->error_message();
      return data;
    }
    if (!has_target) {
      AddErrorMessage("Failed to get target for " +
                      data->real_compiler_path(),
                      data.get());
      LOG(ERROR) << data->error_message();
      return data;
    }

    if (!GetExtraSubprograms(local_compiler_path,
                             gcc_flags,
                             compiler_info_envs,
                             data.get())) {
      std::ostringstream ss;
      ss << "Failed to get subprograms for "
         << data->real_compiler_path();
      AddErrorMessage(ss.str(), data.get());
      LOG(ERROR) << data->error_message();
      return data;
    }
    {
      // Since we only support subprograms for gcc/clang,
      // we do not need to rewrite subprogram's hashes on windows clang
      // (clang-cl), MSVS cl.exe and Javac.
      AUTO_SHARED_LOCK(lock, &rwlock_);
      RewriteHashUnlocked(hash_rewrite_rule_, data.get());
    }

    // Hack for GCC 5's has_include and has_include_next support.
    // GCC has built-in macro that defines __has_include to __has_include__
    // and __has_include_next to __has_include_next__.
    // https://gcc.gnu.org/viewcvs/gcc/trunk/gcc/c-family/c-cppbuiltin.c?revision=229533&view=markup#l794
    // However, __has_include__ and __has_include_next__ are usable but not
    // defined.
    // https://gcc.gnu.org/viewcvs/gcc/trunk/libcpp/init.c?revision=229154&view=markup#l376
    // i.e.
    // if we execute gcc -E to followings, we only get
    // "__has_include__(<stddef.h>)"
    //   #ifdef __has_include__
    //   "__has_include__"
    //   #endif
    //   #ifdef __has_include__(<stddef.h>)
    //   "__has_include__(<stddef.h>)"
    //   #endif
    // See also: b/25581637
    //
    // Note that I do not think we need version check because:
    // 1. __has_include is the new feature and old version does not have it.
    // 2. I can hardly think they change their implementation as far as
    //    I guessed from the code
    if (data->name() == "gcc" || data->name() == "g++") {
      bool has_include = false;
      bool has_include__ = false;
      bool has_include_next = false;
      bool has_include_next__ = false;
      for (const auto& m : data->supported_predefined_macros()) {
        if (m == "__has_include")
          has_include = true;
        if (m == "__has_include__")
          has_include__ = true;
        if (m == "__has_include_next")
          has_include_next = true;
        if (m == "__has_include_next__")
          has_include_next__ = true;
      }

      if (has_include && !has_include__ &&
          (data->predefined_macros().find("__has_include__")
           != string::npos)) {
        data->add_hidden_predefined_macros("__has_include__");
      }
      if (has_include_next && !has_include_next__ &&
          (data->predefined_macros().find("__has_include_next__")
           != string::npos)) {
        data->add_hidden_predefined_macros("__has_include_next__");
      }
    }
  } else if (flags.is_vc()) {
    if (CompilerFlags::IsClangClCommand(local_compiler_path)) {
      const VCFlags& vc_flags = static_cast<const VCFlags&>(flags);
      const bool is_clang = true;
      const bool is_clang_tidy = false;
      const string& lang_flag = vc_flags.is_cplusplus()?"/TP":"/TC";
      if (!SetBasicCompilerInfo(abs_local_compiler_path,
                                vc_flags.compiler_info_flags(),
                                compiler_info_envs,
                                vc_flags.cwd(),
                                lang_flag,
                                vc_flags.is_cplusplus(),
                                is_clang,
                                is_clang_tidy,
                                false,
                                data.get())) {
        DCHECK(data->has_error_message());
        // If error occurred in SetBasicCompilerInfo, we do not need to
        // continue.
        return data;
      }

      const string& sharp_output = GetClangClSharpOutput(
          abs_local_compiler_path, vc_flags.compiler_info_flags(),
          compiler_info_envs, vc_flags.cwd());
      if (sharp_output.empty() ||
          !ParseClangVersionTarget(sharp_output,
                                   data->mutable_version(),
                                   data->mutable_target())) {
        AddErrorMessage("Failed to get version string for " +
                        abs_local_compiler_path,
                        data.get());
        LOG(ERROR) << data->error_message();
        return data;
      }
    } else {
      // cl.exe.
      string vcflags_path = GetMyDirectory();
      vcflags_path += "\\vcflags.exe";
      data->set_predefined_macros(
          data->predefined_macros() + flags.implicit_macros());
      if (!GetVCVersion(abs_local_compiler_path, compiler_info_envs,
                        flags.cwd(),
                        data->mutable_version(),
                        data->mutable_target())) {
        AddErrorMessage("Failed to get cl.exe version for " +
                        abs_local_compiler_path,
                        data.get());
        LOG(ERROR) << data->error_message();
        return data;
      }
      if (!GetVCDefaultValues(abs_local_compiler_path,
                              vcflags_path,
                              flags.compiler_info_flags(),
                              compiler_info_envs,
                              flags.cwd(),
                              data->lang(), data.get())) {
        AddErrorMessage("Failed to get cl.exe system include path "
                        " or predifined macros for " + abs_local_compiler_path,
                        data.get());
        LOG(ERROR) << data->error_message();
        return data;
      }
    }
  } else if (flags.is_javac()) {
    if (!GetJavacVersion(local_compiler_path, compiler_info_envs, flags.cwd(),
                         data->mutable_version())) {
      AddErrorMessage("Failed to get java version for " + local_compiler_path,
                      data.get());
      LOG(ERROR) << data->error_message();
      return data;
    }
    data->set_target("java");
  } else if (flags.is_clang_tidy()) {
    if (!GetClangTidyVersionTarget(local_compiler_path,
                                   compiler_info_envs,
                                   flags.cwd(),
                                   data->mutable_version(),
                                   data->mutable_target())) {
      AddErrorMessage(
          "Failed to get clang-tidy version for " + local_compiler_path,
          data.get());
      LOG(ERROR) << data->error_message();
      return data;
    }

    string clang_abs_local_compiler_path =
        file::JoinPath(file::Dirname(abs_local_compiler_path), "clang");

    const ClangTidyFlags& clang_tidy_flags =
        static_cast<const ClangTidyFlags&>(flags);
    const bool is_clang = true;
    const bool is_clang_tidy = true;

    // See the comment in this function where SetBasicCompilerInfo
    // is called in clangs.is_gcc() if-statement.
    if (!SetBasicCompilerInfo(clang_abs_local_compiler_path,
                              clang_tidy_flags.compiler_info_flags(),
                              compiler_info_envs,
                              clang_tidy_flags.cwd(),
                              "-x" + flags.lang(),
                              clang_tidy_flags.is_cplusplus(),
                              is_clang,
                              is_clang_tidy,
                              clang_tidy_flags.has_nostdinc(),
                              data.get())) {
      DCHECK(data->has_error_message());
      // If error occurred in SetBasicCompilerInfo, we do not need to
      // continue.
      AddErrorMessage("Failed to set basic compiler info for "
                      "corresponding clang: " + clang_abs_local_compiler_path,
                      data.get());
      LOG(ERROR) << data->error_message();
      return data;
    }
  } else {
    LOG(FATAL) << "Unknown compiler type";
  }

  return data;
}

/* static */
bool CompilerInfoBuilder::SplitGccIncludeOutput(
    const string& gcc_v_output,
    std::vector<string>* qpaths,
    std::vector<string>* paths,
    std::vector<string>* framework_paths) {
  // TODO: use StringPiece for gcc_v_output etc.

  static const string kQStartMarker("#include \"...\" search starts here:");
  static const string kStartMarker("#include <...> search starts here:");
  static const string kEndMarker("End of search list.");
  size_t qstart_pos = gcc_v_output.find(kQStartMarker);
  size_t start_pos = gcc_v_output.find(kStartMarker);
  size_t end_pos = gcc_v_output.find(kEndMarker);
  if (qstart_pos == string::npos ||
      start_pos == string::npos || end_pos == string::npos) {
    // something is wrong with output from gcc.
    LOG(WARNING) << "gcc output is wrong. " << gcc_v_output;
    return false;
  }
  if (qpaths != nullptr) {
    string gcc_v_qsearch_paths(
        gcc_v_output.substr(
            qstart_pos + kQStartMarker.size(),
            start_pos - qstart_pos - kQStartMarker.size()));
    VLOG(2) << "extracted qsearch paths [" << gcc_v_qsearch_paths << "]";
    qpaths->clear();
    std::vector<string> split_qpaths;
    SplitStringUsing(gcc_v_qsearch_paths, "\r\n", &split_qpaths);
    for (const auto& split_qpath : split_qpaths) {
      StringPiece qpath = StringStrip(split_qpath);
      if (!qpath.empty()) {
        qpaths->emplace_back(string(qpath));
      }
    }
  }

  string gcc_v_search_paths(
      gcc_v_output.substr(
          start_pos + kStartMarker.size(),
          end_pos - start_pos - kStartMarker.size()));
  VLOG(2) << "extracted search paths [" << gcc_v_search_paths << "]";
  paths->clear();
  std::vector<string> split_paths;
  SplitStringUsing(gcc_v_search_paths, "\r\n", &split_paths);
  for (const auto& split_path : split_paths) {
    StringPiece path = StringStrip(split_path);
    if (!path.empty()) {
      static const char* kFrameworkMarker = "(framework directory)";
      if (strings::EndsWith(path, kFrameworkMarker)) {
        if (framework_paths) {
          path.remove_suffix(strlen(kFrameworkMarker));
          path = StringStrip(path);
          framework_paths->emplace_back(path);
        }
      } else {
        paths->emplace_back(path);
      }
    }
  }
  return true;
}

/* static */
bool CompilerInfoBuilder::ParseFeatures(
    const string& feature_output,
    FeatureList object_macros,
    FeatureList function_macros,
    FeatureList features,
    FeatureList extensions,
    FeatureList attributes,
    FeatureList cpp_attributes,
    FeatureList declspec_attributes,
    FeatureList builtins,
    CompilerInfoData* compiler_info) {
  const size_t num_all_features =
      object_macros.second + function_macros.second +
      features.second + extensions.second + attributes.second +
      cpp_attributes.second + declspec_attributes.second +
      builtins.second;
  std::vector<string> lines;
  SplitStringUsing(feature_output, "\n", &lines);

  size_t index = 0;
  int expected_index = -1;
  for (const auto& line : lines) {
    if (line.empty())
      continue;

    if (line[0] == '#' && line.size() > 3) {
      // expects:
      // # <number> "<filename>"
      expected_index = std::atoi(line.c_str() + 2) - 1;
    }

    if (line[0] == '#' || line[0] == '\0')
      continue;

    if (!(isalnum(line[0]) || line[0] == '_')) {
      LOG(ERROR) << "Ignoring expected line in clang's output: "
                 << line;
      continue;
    }

    if (index >= num_all_features) {
      LOG(ERROR) << "The number of known extensions is strange:"
                 << " index=" << index
                 << " feature_output=" << feature_output;
      AddErrorMessage(
          "goma error: unknown feature or extension detected.",
          compiler_info);
      return false;
    }

    size_t current_index = index++;
    LOG_IF(WARNING,
           expected_index < 0 ||
           static_cast<size_t>(expected_index) != current_index)
        << "index seems to be wrong."
        << " current_index=" << current_index
        << " expected_index=" << expected_index;

    // The result is 0 or 1 in most cases.
    // __has_cpp_attribute(xxx) can be 200809, 201309, though.
    // Anyway, we remember the value that is all digit.

    bool all_digit = true;
    for (char c : line) {
      if (!isdigit(c)) {
        all_digit = false;
        break;
      }
    }
    int value = all_digit ? std::atoi(line.c_str()) : 0;
    if (value == 0)
      continue;

    if (current_index < object_macros.second) {
      compiler_info->add_supported_predefined_macros(
          object_macros.first[current_index]);
      continue;
    }
    current_index -= object_macros.second;
    if (current_index < function_macros.second) {
      compiler_info->add_supported_predefined_macros(
          function_macros.first[current_index]);
      continue;
    }
    current_index -= function_macros.second;
    if (current_index < features.second) {
      CompilerInfoData::MacroValue* m = compiler_info->add_has_feature();
      m->set_key(features.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= features.second;
    if (current_index < extensions.second) {
      CompilerInfoData::MacroValue* m = compiler_info->add_has_extension();
      m->set_key(extensions.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= extensions.second;
    if (current_index < attributes.second) {
      CompilerInfoData::MacroValue* m = compiler_info->add_has_attribute();
      m->set_key(attributes.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= attributes.second;
    if (current_index < cpp_attributes.second) {
      CompilerInfoData::MacroValue* m = compiler_info->add_has_cpp_attribute();
      m->set_key(cpp_attributes.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= cpp_attributes.second;
    if (current_index < declspec_attributes.second) {
      CompilerInfoData::MacroValue* m =
          compiler_info->add_has_declspec_attribute();
      m->set_key(declspec_attributes.first[current_index]);
      m->set_value(value);
      continue;
    }
    current_index -= declspec_attributes.second;
    if (current_index < builtins.second) {
      CompilerInfoData::MacroValue* m = compiler_info->add_has_builtin();
      m->set_key(builtins.first[current_index]);
      m->set_value(value);
      continue;
    }

    // Since we've checked index range, must not reach here.
    LOG(FATAL) << "The number of features exceeds the expected number:"
               << " expected=" << num_all_features
               << " actual=" << (index - 1);
  }

  if (index != num_all_features) {
    LOG(ERROR)  << "The number of features should be "
                << "the expected number:"
                << " expected=" << num_all_features
                << " actual=" << index
                << " feature_output=" << feature_output;
    AddErrorMessage(
        "goma error: failed to detect clang features.",
        compiler_info);
    return false;
  }
  return true;
}

/* static */
bool CompilerInfoBuilder::GetPredefinedFeaturesAndExtensions(
    const string& normal_compiler_path,
    const string& lang_flag,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    CompilerInfoData* compiler_info) {
  std::ostringstream oss;

  int index = 0;

  // Check object-like predefined macros are supported.
  // For example, __FILE__, __LINE__, __COUNTER__, ...
  for (int i = 0; i < kPredefinedObjectMacroSize; ++i) {
    oss << "#ifdef " << kPredefinedObjectMacros[i] << "\n"
        << '#' << ++index << '\n'
        << "1\n"
        << "#else\n";
    oss << '#' << index << '\n'
        << "0\n"
        << "#endif\n";
  }

  // Check function-like predefined macros are supported.
  // __has_include(), __has_feature(), __has_extension(), ...
  for (int i = 0; i < kPredefinedFunctionMacroSize; ++i) {
    oss << "#ifdef " << kPredefinedFunctionMacros[i] << "\n"
        << '#' << ++index << '\n'
        << "1\n"
        << "#else\n";
    oss << '#' << index << '\n'
        << "0\n"
        << "#endif\n";
  }

  // Define predefined macros in case they are not defined.
  oss << "#ifndef __has_feature\n"
      << "# define __has_feature(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_extension\n"
      << "# define __has_extension(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_attribute\n"
      << "# define __has_attribute(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_cpp_attribute\n"
      << "# define __has_cpp_attribute(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_declspec_attribute\n"
      << "# define __has_declspec_attribute(x) 0\n"
      << "#endif\n"
      << "#ifndef __has_builtin\n"
      << "# define __has_builtin(x) 0\n"
      << "#endif\n";

  for (size_t i = 0; i < NUM_KNOWN_FEATURES; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    oss << "__has_feature(" << KNOWN_FEATURES[i] << ")\n";
  }
  for (size_t i = 0; i < NUM_KNOWN_EXTENSIONS; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    oss << string("__has_extension(") << KNOWN_EXTENSIONS[i] << ")\n";
  }
  for (size_t i = 0; i < NUM_KNOWN_ATTRIBUTES; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    oss << string("__has_attribute(") << KNOWN_ATTRIBUTES[i] << ")\n";
  }
  // Check this only in c++ mode. In c mode, preprocess will fail.
  for (size_t i = 0; i < NUM_KNOWN_CPP_ATTRIBUTES; i++) {
    // Specify the line number to tell pre-processor to output newlines.
    oss << '#' << ++index << '\n';
    if (lang_flag == "-xc++")
      oss << string("__has_cpp_attribute(") << KNOWN_CPP_ATTRIBUTES[i] << ")\n";
    else
      oss << "0\n";
  }
  for (size_t i = 0; i < NUM_KNOWN_DECLSPEC_ATTRIBUTES; i++) {
    oss << '#' << ++index << '\n';
    oss << "__has_declspec_attribute(" << KNOWN_DECLSPEC_ATTRIBUTES[i] << ")\n";
  }
  for (size_t i = 0; i < NUM_KNOWN_BUILTINS; i++) {
    oss << '#' << ++index << '\n';
    oss << "__has_builtin(" << KNOWN_BUILTINS[i] << ")\n";
  }

  const string& source = oss.str();
  VLOG(1) << "source=" << source;

  ScopedTmpFile tmp_file("goma_compiler_proxy_check_features_");
  if (!tmp_file.valid()) {
    PLOG(ERROR) << "failed to make temp file: " << tmp_file.filename();
    AddErrorMessage(
        "goma error: failed to create a temp. file.",
        compiler_info);
    return false;
  }

  ssize_t written = tmp_file.Write(source.data(), source.size());
  if (static_cast<ssize_t>(source.size()) != written) {
    PLOG(ERROR) << "Failed to write source into " << tmp_file.filename()
                << ": " << source.size() << " vs " << written;
    AddErrorMessage("goma error: failed to write a temp file.",
                    compiler_info);
    return false;
  }
  // We do not need to append data to |tmp_file|.
  // Keeping it opened may cause a trouble on Windows.
  // Note: |tmp_file.filename()| is kept until the end of the scope.
  if (!tmp_file.Close()) {
    PLOG(ERROR) << "failed to close temp file: " << tmp_file.filename();
    AddErrorMessage("goma error: failed to close a temp. file.",
                    compiler_info);
    return false;
  }

  std::vector<string> argv;
  argv.push_back(normal_compiler_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back(lang_flag);
  argv.push_back("-E");
  argv.push_back(tmp_file.filename());

  std::vector<string> env;
  env.push_back("LC_ALL=C");
  copy(compiler_info_envs.begin(), compiler_info_envs.end(),
       back_inserter(env));

  int32_t status = 0;
  const string& out = ReadCommandOutput(normal_compiler_path, argv, env, cwd,
                                        STDOUT_ONLY, &status);
  VLOG(1) << "out=" << out;
  LOG_IF(ERROR, status != 0)
      << "Read of features and extensions did not ends with status 0."
      << " normal_compiler_path=" << normal_compiler_path
      << " status=" << status
      << " argv=" << argv
      << " env=" << env
      << " cwd=" << cwd
      << " out=" << out;

  FeatureList object_macros = std::make_pair(
      kPredefinedObjectMacros, kPredefinedObjectMacroSize);
  FeatureList function_macros = std::make_pair(
      kPredefinedFunctionMacros, kPredefinedFunctionMacroSize);
  FeatureList features = std::make_pair(
      KNOWN_FEATURES, NUM_KNOWN_FEATURES);
  FeatureList extensions = std::make_pair(
      KNOWN_EXTENSIONS, NUM_KNOWN_EXTENSIONS);
  FeatureList attributes = std::make_pair(
      KNOWN_ATTRIBUTES, NUM_KNOWN_ATTRIBUTES);
  FeatureList cpp_attributes = std::make_pair(
      KNOWN_CPP_ATTRIBUTES, NUM_KNOWN_CPP_ATTRIBUTES);
  FeatureList declspec_attributes = std::make_pair(
      KNOWN_DECLSPEC_ATTRIBUTES, NUM_KNOWN_DECLSPEC_ATTRIBUTES);
  FeatureList builtins = std::make_pair(
      KNOWN_BUILTINS, NUM_KNOWN_BUILTINS);

  return ParseFeatures(out, object_macros, function_macros,
                       features, extensions, attributes,
                       cpp_attributes, declspec_attributes,
                       builtins,
                       compiler_info);
}

/* static */
bool CompilerInfoBuilder::GetAdditionalFlags(
    const string& cxx_display_output, std::vector<string>* flags) {
  std::vector<string> driver_args;
  if (!ParseDriverArgs(cxx_display_output, &driver_args))
    return false;
  FlagParser flag_parser;
  GCCFlags::DefineFlags(&flag_parser);
  flag_parser.AddBoolFlag("fuse-init-array")->SetOutput(flags);
  flag_parser.Parse(driver_args);

  return true;
}

/* static */
bool CompilerInfoBuilder::GetResourceDir(const string& c_display_output,
                                         CompilerInfoData* compiler_info) {
  std::vector<string> driver_args;
  if (!ParseDriverArgs(c_display_output, &driver_args))
    return false;

  FlagParser flag_parser;
  GCCFlags::DefineFlags(&flag_parser);

  FlagParser::Flag* resource_dir = flag_parser.AddFlag("resource-dir");
  flag_parser.Parse(driver_args);

  if (!resource_dir->seen())
    return false;

  string dir = resource_dir->GetLastValue();
  if (dir.empty())
    return false;

  compiler_info->set_resource_dir(dir);
  return true;
}

class GetClangPluginPath : public FlagParser::Callback {
 public:
  GetClangPluginPath(
      std::vector<string>* subprograms)
      : load_seen_(false), subprograms_(subprograms) {}
  ~GetClangPluginPath() override {}

  string ParseFlagValue(const FlagParser::Flag& flag ALLOW_UNUSED,
                        const string& value) override {
    if (load_seen_) {
      load_seen_ = false;
      if (!used_plugin_.insert(value).second) {
        LOG(INFO) << "The same plugin is trying to be added more than twice."
                  << " Let us ignore it to reduce subprogram spec size."
                  << " path=" << value;
      }
      subprograms_->push_back(value);
    }
    if (value == "-load") {
      load_seen_ = true;
    }
    return value;
  }

 private:
  bool load_seen_;
  std::vector<string>* subprograms_;
  std::set<string> used_plugin_;
};

/* static */
bool CompilerInfoBuilder::GetExtraSubprograms(
    const string& normal_gcc_path,
    const GCCFlags& gcc_flags,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* compiler_info) {
  // TODO: support linker subprograms on linking.
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  std::set<string> known_subprograms;
  CompilerInfoBuilder::ParseSubprogramFlags(normal_gcc_path, gcc_flags,
                                            &clang_plugins,
                                            &B_options,
                                            &no_integrated_as);
  for (const auto& path : clang_plugins) {
    string absolute_path = file::JoinPathRespectAbsolute(gcc_flags.cwd(), path);
    if (!known_subprograms.insert(absolute_path).second) {
      LOG(INFO) << "ignored duplicated subprogram: " << absolute_path;
      continue;
    }
    if (!AddSubprogramInfo(absolute_path,
                           compiler_info->mutable_subprograms())) {
      LOG(ERROR) << "invalid plugin:"
                 << " absolute_path=" << absolute_path
                 << " normal_gcc_path=" << normal_gcc_path
                 << " compiler_info_flags="
                 << gcc_flags.compiler_info_flags();
      return false;
    }
  }

  std::vector<string> subprogram_paths;
  if (!CompilerInfoBuilder::GetSubprograms(normal_gcc_path, gcc_flags.lang(),
                                           gcc_flags.compiler_info_flags(),
                                           compiler_info_envs, gcc_flags.cwd(),
                                           no_integrated_as,
                                           &subprogram_paths)) {
    LOG(ERROR) << "failed to get subprograms.";
    return false;
  }
  if (no_integrated_as && !CompilerInfoBuilder::HasAsPath(subprogram_paths)) {
    LOG(ERROR) << "no_integrated_as is set but we cannot find as.";
    return false;
  }
  for (const auto& path : subprogram_paths) {
    bool may_register = false;
    if (no_integrated_as && strings::EndsWith(path, "as")) {
      may_register = true;
    } else {
      // List only subprograms under -B path for backward compatibility.
      // See b/63082235
      for (const string& b : B_options) {
        if (strings::StartsWith(path, b)) {
          may_register = true;
          break;
        }
      }
    }
    if (!may_register) {
      LOG(INFO) << "showed up as subprogram but not sent for"
                << " backword compatibility."
                << " path=" << path
                << " normal_gcc_path=" << normal_gcc_path
                << " compiler_info_flags=" << gcc_flags.compiler_info_flags();
      continue;
    }

    string absolute_path = file::JoinPathRespectAbsolute(gcc_flags.cwd(), path);
    if (!known_subprograms.insert(absolute_path).second) {
      LOG(INFO) << "ignored duplicated subprogram: " << absolute_path;
      continue;
    }
    if (!AddSubprogramInfo(absolute_path,
                           compiler_info->mutable_subprograms())) {
      LOG(ERROR) << "invalid subprogram:"
                 << " absolute_path=" << absolute_path
                 << " normal_gcc_path=" << normal_gcc_path
                 << " compiler_info_flags="
                 << gcc_flags.compiler_info_flags();
      return false;
    }
  }

  return true;
}

/* static */
void CompilerInfoBuilder::ParseSubprogramFlags(
    const string& normal_gcc_path,
    const GCCFlags& gcc_flags,
    std::vector<string>* clang_plugins,
    std::vector<string>* B_options,
    bool* no_integrated_as) {
  const std::vector<string>& compiler_info_flags =
      gcc_flags.compiler_info_flags();
  FlagParser flag_parser;
  GCCFlags::DefineFlags(&flag_parser);

  // Clang plugin support.
  GetClangPluginPath get_clang_plugin_path(clang_plugins);
  flag_parser.AddFlag("Xclang")->SetCallbackForParsedArgs(
      &get_clang_plugin_path);

  // Support no-integrated-as.
  flag_parser.AddBoolFlag("no-integrated-as")->SetSeenOutput(
      no_integrated_as);
  flag_parser.AddBoolFlag("fno-integrated-as")->SetSeenOutput(
      no_integrated_as);

  // Parse -B options.
  FlagParser::Flag* flag_B = flag_parser.AddBoolFlag("B");

  std::vector<string> argv;
  argv.push_back(normal_gcc_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  flag_parser.Parse(argv);

  std::copy(flag_B->values().cbegin(), flag_B->values().cend(),
            std::back_inserter(*B_options));
}


/* static */
void CompilerInfoBuilder::ParseGetSubprogramsOutput(
    const string& gcc_output, std::vector<string>* paths) {
  const std::vector<string> candidates = {
    "as", "objcopy", "cc1", "cc1plus", "cpp", "nm"};
  std::set<string> known;

  std::vector<string> lines;
  SplitStringUsing(gcc_output, "\r\n", &lines);
  for (const auto& line : lines) {
    if (line.empty() || line[0] != ' ')
      continue;
    std::vector<string> argv;
    // Since clang is not used on Windows now, this won't be the issue.
    ParsePosixCommandLineToArgv(line, &argv);
    if (argv.size() == 0)
      continue;
    const string& cmd = argv[0];
    StringPiece basename = file::Basename(cmd);
    if (basename == cmd) {
      // To keep backword compatibility, we do not add subprogram searched
      // in PATH.
      LOG(INFO) << "ignore subprogram searched in PATH."
                << " cmd=" << cmd;
      continue;
    }
    if (!known.insert(cmd).second) {
      continue;
    }
    for (const auto& candidate : candidates) {
      if (basename == candidate ||
          strings::EndsWith(basename, "-" + candidate)) {
        paths->push_back(cmd);
        break;
      }
    }
  }
}

/* static */
bool CompilerInfoBuilder::GetSubprograms(
    const string& gcc_path,
    const string& lang,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    bool warn_on_empty,
    std::vector<string>* subprogs) {
  std::vector<string> argv = {gcc_path};
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  // Since a compiler returns EXIT_FAILURE if fails to output file,
  // we need to use a fake temporary file.
  // Failure of writing *.dwo might be the reason.
  ScopedTmpDir tmp("get_subprograms");
  if (!tmp.valid()) {
    LOG(ERROR) << "cannot make an empty directory";
    return false;
  }
#ifdef _WIN32
  // This code is used by NaCl gcc, PNaCl clang on Windows.
  // Former uses /dev/null as null device, and latter recently uses NUL as
  // null device.  To provide the same code to both, let me use temporary
  // file for that.
  ScopedTmpFile tmpfile("get_subprograms");
  if (!tmpfile.valid()) {
    LOG(ERROR) << "cannot make an empty file";
    return false;
  }
  tmpfile.Close();
  const string& empty_file = tmpfile.filename();
  VLOG(2) << "empty_file=" << empty_file;
#else
  const string& empty_file = "/dev/null";
#endif
  const string output_file = file::JoinPath(tmp.dirname(), "output");
  VLOG(2) << "output_file=" << output_file;
  argv.emplace_back("-x" + lang);
  argv.emplace_back("-c");
  argv.emplace_back(empty_file);
  argv.emplace_back("-o");
  argv.emplace_back(output_file);
  argv.emplace_back("-v");
  int32_t status;
  string gcc_output(
      ReadCommandOutput(gcc_path, argv, compiler_info_envs, cwd,
                        MERGE_STDOUT_STDERR, &status));
  if (status != 0) {
    LOG(ERROR)
      << "ReadCommandOutput exited with non zero status code."
      << " gcc_path=" << gcc_path
      << " status=" << status
      << " argv=" << argv
      << " env=" << compiler_info_envs
      << " cwd=" << cwd
      << " gcc_output=" << gcc_output;
    return false;
  }
  VLOG(1) << "GetSubprograms:"
      << " gcc_path=" << gcc_path
      << " status=" << status
      << " argv=" << argv
      << " env=" << compiler_info_envs
      << " cwd=" << cwd
      << " gcc_output=" << gcc_output;
  CompilerInfoBuilder::ParseGetSubprogramsOutput(gcc_output, subprogs);
  LOG_IF(ERROR, warn_on_empty && subprogs->empty())
      << "Expect to have at least one subprograms but empty."
      << " gcc_path=" << gcc_path
      << " status=" << status
      << " argv=" << argv
      << " env=" << compiler_info_envs
      << " cwd=" << cwd
      << " gcc_output=" << gcc_output;
  return true;
}

/* static */
bool CompilerInfoBuilder::HasAsPath(
    const std::vector<string>& subprogram_paths) {
  for (const auto& path : subprogram_paths) {
    StringPiece basename = file::Basename(path);
    if (basename == "as" || strings::EndsWith(basename, "-as")) {
      return true;
    }
  }
  return false;
}

/* static */
string CompilerInfoBuilder::ParseRealClangPath(StringPiece v_out) {
  StringPiece::size_type pos = v_out.find_first_of('"');
  if (pos == StringPiece::npos)
    return "";
  v_out.remove_prefix(pos + 1);
  pos = v_out.find_first_of('"');
  if (pos == StringPiece::npos)
    return "";
  v_out = v_out.substr(0, pos);
  if (!CompilerFlags::IsClangCommand(v_out))
    return "";
  return string(v_out);
}

#if defined(__linux__) || defined(__MACH__)
static string GetRealClangPath(const string& normal_gcc_path,
                               const string& cwd,
                               const std::vector<string>& envs) {
  std::vector<string> argv;
  argv.push_back(normal_gcc_path);
  argv.push_back("-xc");
  argv.push_back("-v");
  argv.push_back("-E");
  argv.push_back("/dev/null");
  int32_t status = 0;
  const string v_output = ReadCommandOutput(normal_gcc_path, argv, envs, cwd,
                                            MERGE_STDOUT_STDERR, &status);
  LOG_IF(ERROR, status != 0)
      << "ReadCommandOutput exited with non zero status code."
      << " normal_gcc_path=" << normal_gcc_path
      << " status=" << status
      << " argv=" << argv
      << " envs=" << envs
      << " cwd=" << cwd
      << " v_output=" << v_output;
  const string clang_path = CompilerInfoBuilder::ParseRealClangPath(v_output);
  if (!clang_path.empty() && access(clang_path.c_str(), X_OK) == 0)
    return clang_path;
  return string();
}
#endif

/* static */
string CompilerInfoBuilder::GetRealCompilerPath(
    const string& normal_gcc_path,
    const string& cwd,
    const std::vector<string>& envs) {
#if !defined(__linux__) && !defined(__MACH__) && !defined(_WIN32)
  return normal_gcc_path;
#endif

#if defined(__linux__) || defined(__MACH__)
  // For whom using a wrapper script for clang.
  // E.g. ChromeOS clang and Android.
  //
  // Since clang invokes itself as cc1, we can find its real name by capturing
  // what is cc1.  Exception is that it is invoked via a shell script that
  // invokes loader, which might be only done by ChromeOS clang.
  //
  // For pnacl-clang, although we still use binary_hash of local_compiler for
  // command_spec in request, we also need real compiler to check toolchain
  // update for compiler_info_cache.
  if (CompilerFlags::IsClangCommand(normal_gcc_path)) {
    const string real_path = GetRealClangPath(normal_gcc_path, cwd, envs);
    if (real_path.empty()) {
      LOG(WARNING) << "seems not be a clang?"
                   << " normal_gcc_path=" << normal_gcc_path;
      return normal_gcc_path;
    }
#ifndef __linux__
    return real_path;
#else
    // Ubuntu Linux is required to build ChromeOS.
    // We do not need to consider ChromeOS clang for Mac.
    // http://www.chromium.org/chromium-os/quick-start-guide
    //
    // Consider the clang is ChromeOS clang, which runs via a wrapper.
    // TODO: more reliable ways?
    string real_chromeos_clang_path = real_path + ".elf";
    if (access(real_chromeos_clang_path.c_str(), X_OK) == 0) {
      return real_chromeos_clang_path;
    } else {
      return real_path;
    }
#endif
  }
#endif

#ifdef __linux__
  // For ChromeOS compilers.
  // Note: Ubuntu Linux is required to build ChromeOS.
  // http://www.chromium.org/chromium-os/quick-start-guide
  std::vector<string> argv;
  argv.push_back(normal_gcc_path);
  argv.push_back("-v");
  int32_t status = 0;
  const string& v_output = ReadCommandOutput(normal_gcc_path, argv, envs, cwd,
                                             MERGE_STDOUT_STDERR, &status);
  LOG_IF(ERROR, status != 0)
      << "ReadCommandOutput exited with non zero status code."
      << " normal_gcc_path=" << normal_gcc_path
      << " status=" << status
      << " argv=" << argv
      << " envs=" << envs
      << " cwd=" << cwd
      << " v_output=" << v_output;
  const char* kCollectGcc = "COLLECT_GCC=";
  size_t index = v_output.find(kCollectGcc);
  if (index == string::npos)
    return normal_gcc_path;
  index += strlen(kCollectGcc);

  // If COLLECT_GCC is specified and gcc is accompanied by gcc.real,
  // we assume the "real" one is the last binary we will run.
  // TODO: More reliable ways?
  const string& gcc_path =
      v_output.substr(index, v_output.find_first_of("\r\n", index) - index);
  const string& real_gcc_path = gcc_path + ".real";
  if (access(real_gcc_path.c_str(), R_OK) == 0) {
    return real_gcc_path;
  }
  return gcc_path;
#endif

#ifdef __MACH__
  if (file::Dirname(normal_gcc_path) != "/usr/bin") {
    return normal_gcc_path;
  }
  const string clang_path = GetRealClangPath(normal_gcc_path, cwd, envs);
  if (!clang_path.empty()) {
    return clang_path;
  }
  LOG(INFO) << "The command seems not clang. Use it as-is: "
            << normal_gcc_path;
  return normal_gcc_path;
#endif

#ifdef _WIN32
  // For Windows nacl-{gcc,g++}.
  // The real binary is ../libexec/nacl-{gcc,g++}.exe.  Binaries under
  // the bin directory are just wrappers to them.
  if (CompilerFlags::IsNaClGCCCommand(normal_gcc_path)) {
    const string& candidate_path = file::JoinPath(
        GetNaClToolchainRoot(normal_gcc_path),
        file::JoinPath("libexec", file::Basename(normal_gcc_path)));
    if (access(candidate_path.c_str(), X_OK) == 0)
      return candidate_path;
  }
  return normal_gcc_path;
#endif
}

/* static */
string CompilerInfoBuilder::GetRealSubprogramPath(
    const string& subprog_path) {
#ifndef __linux__
  return subprog_path;
#else
  // Currently, we only see objcopy runs via shell script wrapper, and
  // nothing else (i.e. no as or so). (b/30571185)
  if (file::Basename(subprog_path) != "objcopy") {
    return subprog_path;
  }

  // Assume ChromeOS objcopy is always in
  // "<target arch>/binutils-bin/<version>-gold/objcopy",
  // and real objcopy is in
  // "<target arch>/binutils-bin/<version>/objcopy.elf".
  if (file::Basename(
      file::Dirname(file::Dirname(subprog_path))) != "binutils-bin") {
    return subprog_path;
  }
  StringPiece dirname = file::Dirname(subprog_path);
  static const char kGoldSuffix[] = "-gold";
  if (strings::EndsWith(dirname, kGoldSuffix)) {
    dirname.remove_suffix(sizeof(kGoldSuffix) - 1);
  }
  const string new_subprog_path = file::JoinPath(dirname, "objcopy.elf");
  FileId new_id(new_subprog_path);
  if (!new_id.IsValid()) {
    LOG(INFO) << ".elf does not exist, might not be chromeos path?"
              << " expect to exist=" << new_subprog_path
              << " orignal subprog_path=" << subprog_path;
    return subprog_path;
  }
  LOG(INFO) << "Hack for objcopy used for ChromeOS simple chrome build:"
            << " apparent subprog_path=" << subprog_path
            << " real subprog_path=" << new_subprog_path;
  return new_subprog_path;
#endif
}

/* static */
bool CompilerInfoBuilder::ParseJavacVersion(const string& version_info,
                                            string* version) {
  version->assign(string(StringRstrip(version_info)));
  static const char kJavac[] = "javac ";
  static const size_t kJavacLength = sizeof(kJavac) - 1;  // Removed '\0'.
  if (!strings::StartsWith(*version, kJavac)) {
    LOG(ERROR) << "Unable to parse javac -version output:"
               << *version;
    return false;
  }
  version->erase(0, kJavacLength);
  return true;
}

/* static */
bool CompilerInfoBuilder::GetJavacVersion(
    const string& javac,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    string* version) {
  std::vector<string> argv;
  argv.push_back(javac);
  argv.push_back("-version");
  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");
  int32_t status = 0;
  bool ret = ParseJavacVersion(
      ReadCommandOutput(javac, argv, env, cwd, MERGE_STDOUT_STDERR, &status),
      version);
  LOG_IF(ERROR, status != 0)
      << "ReadCommandOutput exited with non zero status code."
      << " javac=" << javac
      << " status=" << status
      << " argv=" << argv
      << " env=" << env
      << " cwd=" << cwd;
  return ret;
}

/* static */
bool CompilerInfoBuilder::ParseVCVersion(
    const string& vc_logo, string* version, string* target) {
  // VC's logo format:
  // ... Version 16.00.40219.01 for 80x86
  // so we return cl 16.00.40219.01
  string::size_type pos = vc_logo.find("Version ");
  string::size_type pos2 = vc_logo.find(" for");
  string::size_type pos3 = vc_logo.find("\r");
  if (pos == string::npos || pos2 == string::npos || pos3 == string::npos ||
      pos2 < pos || pos3 < pos2) {
    LOG(INFO) << "Unable to parse cl.exe output."
               << " vc_logo=" << vc_logo;
    return false;
  }
  pos += 8;  // 8: length of "Version "
  *version = vc_logo.substr(pos, pos2 - pos);
  *target = vc_logo.substr(pos2 + 5, pos3 - pos2 - 5);
  return true;
}


/* static */
bool CompilerInfoBuilder::GetVCVersion(
    const string& cl_exe_path, const std::vector<string>& env,
    const string& cwd,
    string* version, string* target) {
  std::vector<string> argv;
  argv.push_back(cl_exe_path);
  int32_t status = 0;
  string vc_logo(ReadCommandOutput(cl_exe_path, argv, env, cwd,
                                   MERGE_STDOUT_STDERR, &status));
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " cl_exe_path=" << cl_exe_path
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " vc_logo=" << vc_logo;
    return false;
  }
  if (!CompilerInfoBuilder::ParseVCVersion(vc_logo, version, target)) {
    LOG(ERROR) << "Failed to parse VCVersion."
               << " cl_exe_path=" << cl_exe_path
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " vc_logo=" << vc_logo;
    return false;
  }
  return true;
}

/* static */
bool CompilerInfoBuilder::ParseVCOutputString(const string& output,
                                       std::vector<string>* include_paths,
                                       string* predefined_macros) {
  std::vector<string> args;
  // |output| doesn't contains command name, so adds "cl.exe" here.
  args.push_back("cl.exe");
  if (!ParseWinCommandLineToArgv(output, &args)) {
    LOG(ERROR) << "Fail parse cmdline:" << output;
    return false;
  }

  VCFlags flags(args, ".");
  if (!flags.is_successful()) {
    LOG(ERROR) << "ParseVCOutput error:" << flags.fail_message();
    return false;
  }

  copy(flags.include_dirs().begin(), flags.include_dirs().end(),
       back_inserter(*include_paths));

  if (predefined_macros == nullptr)
    return true;
  std::ostringstream ss;
  for (const auto& elm : flags.commandline_macros()) {
    const string& macro = elm.first;
    DCHECK(elm.second) << macro;
    size_t found = macro.find('=');
    if (found == string::npos) {
      ss << "#define " << macro << "\n";
    } else {
      ss << "#define " << macro.substr(0, found)
         << " " << macro.substr(found + 1)
         << "\n";
    }
  }
  *predefined_macros += ss.str();
  return true;
}

/* static */
bool CompilerInfoBuilder::ParseClangVersionTarget(
    const string& sharp_output,
    string* version, string* target) {
  static const char* kTarget = "Target: ";
  std::vector<string> lines;
  SplitStringUsing(sharp_output, "\r\n", &lines);
  if (lines.size() < 2)
    return false;
  if (!strings::StartsWith(lines[1], kTarget))
    return false;
  version->assign(lines[0]);
  target->assign(lines[1].substr(strlen(kTarget)));
  return true;
}

/* static */
bool CompilerInfoBuilder::GetClangTidyVersionTarget(
    const string& clang_tidy_path,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    string* version,
    string* target) {
  std::vector<string> argv;
  argv.push_back(clang_tidy_path);
  argv.push_back("-version");

  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");

  int32_t status = 0;
  const string output(ReadCommandOutput(clang_tidy_path, argv, env, cwd,
                                        MERGE_STDOUT_STDERR, &status));

  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " clang_tidy_path=" << clang_tidy_path
               << " status=" << status
               << " argv=" << argv
               << " env=" << env
               << " cwd=" << cwd
               << " output=" << output;
    return false;
  }

  return ParseClangTidyVersionTarget(output, version, target);
}

/* static */
bool CompilerInfoBuilder::ParseClangTidyVersionTarget(const string& output,
                                                      string* version,
                                                      string* target) {
  static const char kVersion[] = "  LLVM version ";
  static const char kTarget[] = "  Default target: ";

  std::vector<string> lines;
  SplitStringUsing(output, "\r\n", &lines);

  if (lines.size() < 4)
    return false;
  if (!strings::StartsWith(lines[1], kVersion))
    return false;
  if (!strings::StartsWith(lines[3], kTarget))
    return false;

  *version = lines[1].substr(strlen(kVersion));
  *target = lines[3].substr(strlen(kTarget));

  return true;
}

static string GccDisplayPrograms(
    const string& normal_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& lang_flag,
    const string& option,
    const string& cwd,
    int32_t* status) {
  std::vector<string> argv;
  argv.push_back(normal_compiler_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
  argv.push_back(lang_flag);
  if (!option.empty()) {
    if (CompilerFlags::IsClangClCommand(normal_compiler_path)) {
      argv.push_back("-Xclang");
    }
    argv.push_back(option);
  }
#ifdef _WIN32
  // This code is used by NaCl gcc, PNaCl clang and clang-cl on Windows.
  // Former uses /dev/null as null device, and latter recently uses NUL as
  // null device.  To provide the same code to both, let me use temporary
  // file for that.
  ScopedTmpFile tmp("gcc_display_program");
  if (!tmp.valid()) {
    LOG(ERROR) << "cannot make an empty file";
    *status = -1;
    return "";
  }
  tmp.Close();
  const string& empty_file = tmp.filename();
  VLOG(2) << "empty_file=" << empty_file;
#else
  const string& empty_file = "/dev/null";
#endif
  argv.push_back("-v");
  argv.push_back("-E");
  argv.push_back(empty_file);
  argv.push_back("-o");
  argv.push_back(empty_file);

  std::vector<string> env;
  env.push_back("LC_ALL=C");
  copy(compiler_info_envs.begin(), compiler_info_envs.end(),
       back_inserter(env));

  return ReadCommandOutput(normal_compiler_path, argv, env, cwd,
                           MERGE_STDOUT_STDERR, status);
}

// Return true if everything is fine, and all necessary information
// (system include paths, predefined macro, etc) are set to |compiler_info|.
// Otherwise false, and |compiler_info->error_message| is set.
//
// |local_compiler_path| is compiler path.
// |compiler_info_flags| is used as command line options to get info.
// |compiler_info_envs| is used as environment to get info.
// |cwd| is current working directory on getting info.
// |lang_flag| specifies the language to get predefined macros and features.
// e.g. clang -dM |lang_flag| -E /dev/null
// It is usually -xc and -xc++ on gcc variants, but there are also other
// languages such as c-header, cpp-output.  We should use it.
// Currently, Objective-C++ and C++ need to be treated as C++, and
// I believe CompilerFlags should be point of decision of is_cplusplus
// judgement.  For that reason, |is_cplusplus| is passed regardless
// of what lang_flag is used.
// |is_clang| indicates the compiler is clang, so need to get features,
// extensions etc.
// static
bool CompilerInfoBuilder::SetBasicCompilerInfo(
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang_flag,
    bool is_cplusplus,
    bool is_clang,
    bool is_clang_tidy,
    bool has_nostdinc,
    CompilerInfoData* compiler_info) {
  // cxx_lang_flag, c_lang_flag for c++, c respectively.
  // For gcc and clang,
  // even when language is objective-c, objective-c++, c-header, cpp-output,
  // c++-header, c++-cpp-output, we'll use -xc++, -xc to get system include
  // paths.
  // For clang-cl.exe, we use /TP and /TC like we do for gcc and clang.
  string cxx_lang_flag;
  string c_lang_flag;
  if (CompilerFlags::IsClangClCommand(local_compiler_path)) {
    cxx_lang_flag = "/TP";
    c_lang_flag = "/TC";
  } else {
    cxx_lang_flag = "-xc++";
    c_lang_flag = "-xc";
  }

  // We assumes include system paths are same for given compiler_info_flags
  // and compiler_info_envs.
  //
  // We changes the way to get system include path whether it is compiling C++
  // source code or not.
  // C++:
  //   c++ system include path = [paths by -xc++]
  //   c   system include path = [paths by -xc++ -nostdinc++]
  // C:
  //   c   system include path = [paths by -xc]
  //   no need to check C++ system include path.
  //
  // Note that the way to get system include paths are still under discussion
  // in b/13178705.
  string c_output, cxx_output;
  if (is_cplusplus) {
    int32_t status;
    cxx_output = GccDisplayPrograms(
        local_compiler_path,
        compiler_info_flags,
        compiler_info_envs,
        cxx_lang_flag,
        "",
        cwd,
        &status);
    if (status != 0) {
      AddErrorMessage(
          "Failed to execute compiler to get c++ system "
          "include paths for " + local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message()
                 << " status=" << status
                 << " cxx_output=" << cxx_output;
      return false;
    }
    c_output = GccDisplayPrograms(
        local_compiler_path,
        compiler_info_flags,
        compiler_info_envs,
        cxx_lang_flag,
        "-nostdinc++",
        cwd,
        &status);
    if (status != 0) {
      AddErrorMessage(
          "Failed to execute compiler to get c system "
          "include paths for " + local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message()
                 << " status=" << status
                 << " c_output=" << c_output;
      return false;
    }
  } else {
    int32_t status;
    c_output = GccDisplayPrograms(
        local_compiler_path,
        compiler_info_flags,
        compiler_info_envs,
        c_lang_flag,
        "",
        cwd,
        &status);
    if (status != 0) {
      AddErrorMessage(
          "Failed to execute compiler to get c system "
          "include paths for " + local_compiler_path,
          compiler_info);
      LOG(ERROR) << compiler_info->error_message()
                 << " status=" << status
                 << " c_output=" << c_output;
      return false;
    }
  }

  if (!GetSystemIncludePaths(local_compiler_path,
                             compiler_info_flags,
                             compiler_info_envs,
                             cxx_output,
                             c_output,
                             is_cplusplus,
                             has_nostdinc,
                             compiler_info)) {
    AddErrorMessage(
        "Failed to get system include paths for " +
        local_compiler_path,
        compiler_info);
    LOG(ERROR) << compiler_info->error_message();
    return false;
  }
  if (!GetPredefinedMacros(local_compiler_path,
                           compiler_info_flags,
                           compiler_info_envs,
                           cwd,
                           lang_flag,
                           compiler_info)) {
    AddErrorMessage(
        "Failed to get predefined macros for " +
        local_compiler_path,
        compiler_info);
    LOG(ERROR) << compiler_info->error_message();
    return false;
  }

  if (!cxx_output.empty() && !is_clang_tidy) {
    std::vector<string> additional_flags;
    CompilerInfoBuilder::GetAdditionalFlags(cxx_output, &additional_flags);
    for (const auto& f : additional_flags) {
      compiler_info->add_additional_flags(f);
    }
  }
  if (!c_output.empty()) {
    CompilerInfoBuilder::GetResourceDir(c_output, compiler_info);
  }

  if (!GetPredefinedFeaturesAndExtensions(local_compiler_path,
                                          lang_flag,
                                          compiler_info_flags,
                                          compiler_info_envs,
                                          cwd,
                                          compiler_info)) {
    LOG(ERROR) << "Failed to get predefined features and extensions."
               << " local_compiler_path=" << local_compiler_path
               << " lang_flag=" << lang_flag;
    DCHECK(compiler_info->has_error_message());
    return false;
  }
  return true;
}

// static
void CompilerInfoBuilder::UpdateIncludePaths(
    const std::vector<string>& paths,
    google::protobuf::RepeatedPtrField<string>* include_paths) {
  std::copy(paths.cbegin(), paths.cend(),
            google::protobuf::RepeatedFieldBackInserter(include_paths));
}

// static
bool CompilerInfoBuilder::GetSystemIncludePaths(
    const string& normal_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cxx_display_output,
    const string& c_display_output,
    bool is_cplusplus,
    bool has_nostdinc,
    CompilerInfoData* compiler_info) {
  compiler_info->clear_quote_include_paths();
  compiler_info->clear_cxx_system_include_paths();
  compiler_info->clear_system_include_paths();
  compiler_info->clear_system_framework_paths();

  std::vector<string> quote_include_paths;
  std::vector<string> cxx_system_include_paths;
  std::vector<string> system_framework_paths;
  if (cxx_display_output.empty() ||
      !CompilerInfoBuilder::SplitGccIncludeOutput(
          cxx_display_output,
          &quote_include_paths,
          &cxx_system_include_paths,
          &system_framework_paths)) {
    LOG_IF(WARNING, is_cplusplus)
        << "Cannot detect g++ system include paths:"
        << " normal_compiler_path=" << normal_compiler_path
        << " compiler_info_flags=" << compiler_info_flags
        << " compiler_info_envs=" << compiler_info_envs;
  }

  UpdateIncludePaths(
      quote_include_paths,
      compiler_info->mutable_quote_include_paths());

  UpdateIncludePaths(
      cxx_system_include_paths,
      compiler_info->mutable_cxx_system_include_paths());

  UpdateIncludePaths(
      system_framework_paths,
      compiler_info->mutable_system_framework_paths());

  std::vector<string>* quote_include_paths_ptr = nullptr;
  // If quote_include_paths couldn't be obtained above,
  // we'll try to fetch them here.
  if (compiler_info->quote_include_paths_size() == 0) {
    DCHECK(quote_include_paths.empty());
    quote_include_paths_ptr = &quote_include_paths;
  }

  std::vector<string>* framework_paths_ptr = nullptr;
  // If system_framework_paths couldn't be obtained above,
  // we'll try to fetch them here.
  if (compiler_info->system_framework_paths_size() == 0) {
    DCHECK(system_framework_paths.empty());
    framework_paths_ptr = &system_framework_paths;
  }
  std::vector<string> system_include_paths;
  if (!CompilerInfoBuilder::SplitGccIncludeOutput(
          c_display_output,
          quote_include_paths_ptr,
          &system_include_paths,
          framework_paths_ptr)) {
    LOG(WARNING) << "Cannot detect gcc system include paths:"
                 << " normal_compiler_path=" << normal_compiler_path
                 << " compiler_info_flags=" << compiler_info_flags
                 << " compiler_info_envs=" << compiler_info_envs;
  }
  if (quote_include_paths_ptr != nullptr) {
    UpdateIncludePaths(
        quote_include_paths,
        compiler_info->mutable_quote_include_paths());
  }

  UpdateIncludePaths(
      system_include_paths,
      compiler_info->mutable_system_include_paths());

  if (framework_paths_ptr != nullptr) {
    UpdateIncludePaths(
        system_framework_paths,
        compiler_info->mutable_system_framework_paths());
  }

  if (compiler_info->cxx_system_include_paths_size() == 0 &&
      compiler_info->system_include_paths_size() == 0 &&
      !has_nostdinc) {
    std::stringstream ss;
    ss << "Cannot detect system include paths:"
       << " normal_compiler_path=" << normal_compiler_path
       << " compiler_info_flags=" << compiler_info_flags
       << " compiler_info_envs=" << compiler_info_envs
       << " cxx_display_output=" << cxx_display_output
       << " c_display_output=" << c_display_output;
    AddErrorMessage(ss.str(), compiler_info);
    LOG(ERROR) << ss.str();
    return false;
  }

#ifdef _WIN32
  // In the (build: Windows, target: NaCl (not PNaCl)) compile,
  // include paths under toolchain root are shown as relative path from it.
  if (CompilerFlags::IsNaClGCCCommand(normal_compiler_path)) {
    compiler_info->set_toolchain_root(
        GetNaClToolchainRoot(normal_compiler_path));
  }
#endif
  return true;
}

static string GccDisplayPredefinedMacros(
    const string& normal_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang_flag,
    int32_t* status) {
  std::vector<string> argv;
  argv.push_back(normal_compiler_path);
  copy(compiler_info_flags.begin(), compiler_info_flags.end(),
       back_inserter(argv));
#ifdef _WIN32
  // This code is used by NaCl gcc, PNaCl clang and clang-cl on Windows.
  // Former uses /dev/null as null device, and latter recently uses NUL as
  // null device.  To provide the same code to both, let me use temporary
  // file for that.
  ScopedTmpFile tmp("gcc_display_predefined_macro");
  if (!tmp.valid()) {
    LOG(ERROR) << "cannot make an empty file";
    *status = -1;
    return "";
  }
  tmp.Close();
  const string& empty_file = tmp.filename();
  VLOG(2) << "empty_file=" << empty_file;
#else
  const string& empty_file = "/dev/null";
#endif

  argv.push_back(lang_flag);
  argv.push_back("-E");
  argv.push_back(empty_file);
  if (CompilerFlags::IsClangClCommand(normal_compiler_path)) {
    argv.push_back("-Xclang");
  }
  argv.push_back("-dM");

  std::vector<string> env;
  env.push_back("LC_ALL=C");
  copy(compiler_info_envs.begin(), compiler_info_envs.end(),
       back_inserter(env));

  const string& macros = ReadCommandOutput(normal_compiler_path, argv, env, cwd,
                                           MERGE_STDOUT_STDERR, status);
  if (*status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " normal_compiler_path=" << normal_compiler_path
               << " status=" << status
               << " argv=" << argv
               << " env=" << env << " cwd=" << cwd
               << " macros=" << macros;
    return "";
  }
  return macros;
}

// static
bool CompilerInfoBuilder::GetPredefinedMacros(
    const string& normal_compiler_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang_flag,
    CompilerInfoData* compiler_info) {
  int32_t status;
  const string& macros =
      GccDisplayPredefinedMacros(
          normal_compiler_path, compiler_info_flags, compiler_info_envs, cwd,
          lang_flag, &status);
  if (status != 0)
    return false;
  compiler_info->set_predefined_macros(macros);
  return true;
}

// static
bool CompilerInfoBuilder::GetVCDefaultValues(
    const string& cl_exe_path,
    const string& vcflags_path,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    const string& lang,
    CompilerInfoData* compiler_info) {
  // VC++ accepts two different undocumented flags to dump all predefined values
  // in preprocessor.  /B1 is for C and /Bx is for C++.
  string vc_cpp_flags = "/Bx";
  string vc_c_flags = "/B1";
  vc_cpp_flags += vcflags_path;
  vc_c_flags += vcflags_path;

  // It does not matter that non-exist-file.cpp/.c is on disk or not.  VCFlags
  // will error out cl.exe and display the information we want before actually
  // opening that file.
  string output_cpp = GetVCOutputString(cl_exe_path, vc_cpp_flags,
      "non-exist-file.cpp", compiler_info_flags, compiler_info_envs, cwd);
  string output_c = GetVCOutputString(cl_exe_path, vc_c_flags,
      "non-exist-file.c", compiler_info_flags, compiler_info_envs, cwd);

  std::vector<string> cxx_system_include_paths;
  if (!CompilerInfoBuilder::ParseVCOutputString(
          output_cpp, &cxx_system_include_paths,
          lang == "c++" ?
          compiler_info->mutable_predefined_macros() : nullptr)) {
    return false;
  }
  for (const auto& p : cxx_system_include_paths) {
    compiler_info->add_cxx_system_include_paths(p);
  }
  std::vector<string> system_include_paths;
  if (!CompilerInfoBuilder::ParseVCOutputString(
          output_c, &system_include_paths,
          lang == "c" ?
          compiler_info->mutable_predefined_macros() : nullptr)) {
    return false;
  }
  for (const auto& p : system_include_paths) {
    compiler_info->add_system_include_paths(p);
  }
  return true;
}

/* static */
void CompilerInfoBuilder::AddErrorMessage(
    const std::string& message,
    CompilerInfoData* compiler_info) {
  if (compiler_info->failed_at() == 0)
    compiler_info->set_failed_at(time(nullptr));

  if (!compiler_info->has_error_message()) {
    compiler_info->set_error_message(
        compiler_info->error_message() + "\n");
  }
  compiler_info->set_error_message(
      compiler_info->error_message() + message);
}

/* static */
void CompilerInfoBuilder::OverrideError(
    const std::string& message, time_t failed_at,
    CompilerInfoData* compiler_info) {
  DCHECK((message.empty() && failed_at == 0) ||
         (!message.empty() && failed_at > 0));
  compiler_info->set_error_message(message);
  compiler_info->set_failed_at(failed_at);
}

/* static */
bool CompilerInfoBuilder::SubprogramInfoFromPath(
    const string& path, CompilerInfoData::SubprogramInfo* s) {
  FileId file_id(path);
  if (!file_id.IsValid()) {
    return false;
  }
  string hash;
  if (!GomaSha256FromFile(GetRealSubprogramPath(path), &hash)) {
    return false;
  }
  s->set_name(path);
  s->set_hash(hash);
  SetFileIdToData(file_id, s->mutable_file_id());
  return true;
}

void CompilerInfoBuilder::SetHashRewriteRule(
    const std::map<std::string, std::string>& rule) {
  LOG(INFO) << "new hash rewrite rule will be set:"
            << rule;
  AUTO_EXCLUSIVE_LOCK(lock, &rwlock_);
  hash_rewrite_rule_ = rule;
}

/* static */
bool CompilerInfoBuilder::RewriteHashUnlocked(
    const std::map<std::string, std::string>& rule,
    CompilerInfoData* data) {
  if (rule.empty()) {
    return false;
  }

  bool did_rewrite = false;
  for (auto& info : *data->mutable_subprograms()) {
    const auto& found = rule.find(info.hash());
    if (found != rule.end()) {
      VLOG(3) << "rewrite hash of subprograms:"
              << " from=" << info.hash()
              << " to=" << found->second;
      info.set_hash(found->second);
      did_rewrite = true;
    }
  }
  return did_rewrite;
}

std::string CompilerInfoBuilder::GetCompilerName(const CompilerInfoData& data) {
  StringPiece base = file::Basename(data.local_compiler_path());
  if (base != "cc" && base != "c++") {
    // We can simply use local_compiler_path for judging compiler name
    // if basename is not "cc" or "c++".
    // See also b/13107706
    return CompilerFlags::GetCompilerName(data.local_compiler_path());
  }
  if (!CompilerFlags::IsClangCommand(data.real_compiler_path())) {
    return CompilerFlags::GetCompilerName(data.real_compiler_path());
  }
  // clang++ is usually symlink to clang, and real compiler path is
  // usually be clang.  It does not usually reflect what we expect as a
  // compiler name.
  string real_name = CompilerFlags::GetCompilerName(data.real_compiler_path());
  if (base == "cc") {
    return real_name;
  }
  if (real_name == "clang") {
    return string("clang++");
  }
  LOG(WARNING) << "Cannot detect compiler name:"
               << " local=" << data.local_compiler_path()
               << " real=" << data.real_compiler_path();
  return string();
}

void CompilerInfoBuilder::Dump(std::ostringstream* ss) {
  AUTO_SHARED_LOCK(lock, &rwlock_);
  if (hash_rewrite_rule_.empty())
    return;
  *ss << "compiler_info_builder:" << std::endl
      << "  hash_rewrite_rule:" << std::endl;
  for (const auto& entry : hash_rewrite_rule_) {
    *ss << "    " << entry.first << ":" << entry.second << std::endl;
  }
  *ss << std::endl;
}

/* static */
void CompilerInfo::SubprogramInfo::FromData(
    const CompilerInfoData::SubprogramInfo& info_data,
    SubprogramInfo* info) {
  info->name = info_data.name();
  info->hash = info_data.hash();
  GetFileIdFromData(info_data.file_id(), &info->file_id);
}

/* static */
CompilerInfo::SubprogramInfo CompilerInfo::SubprogramInfo::FromPath(
    const string& path) {
  CompilerInfoData::SubprogramInfo data;
  CompilerInfoBuilder::SubprogramInfoFromPath(path, &data);
  CompilerInfo::SubprogramInfo s;
  FromData(data, &s);
  return s;
}

string CompilerInfo::SubprogramInfo::DebugString() const {
  std::stringstream ss;
  ss << "name: " << name;
  ss << ", valid:" << file_id.IsValid();
  ss << ", hash: " << hash;
  return ss.str();
}

string CompilerInfo::DebugString() const {
  return data_->DebugString();
}

bool CompilerInfo::IsUpToDate(const string& local_compiler_path) const {
  FileId cur_local(local_compiler_path);
  if (cur_local != local_compiler_id_) {
    LOG(INFO) << "compiler id is not matched:"
              << " path=" << local_compiler_path
              << " local_compiler_id=" << local_compiler_id_.DebugString()
              << " cur_local=" << cur_local.DebugString();
    return false;
  }
  if (local_compiler_path != data_->real_compiler_path()) {
    // Since |local_compiler_path| != |real_compiler_path|,
    // We need to check that the real compiler is also the same.
    FileId cur_real(data_->real_compiler_path());
    if (cur_real != real_compiler_id_) {
      LOG(INFO) << "real compiler id is not matched:"
                << " local_compiler_path=" << local_compiler_path
                << " real_compiler_path=" << data_->real_compiler_path()
                << " local_compiler_id=" << local_compiler_id_.DebugString()
                << " real_compiler_id=" << real_compiler_id_.DebugString()
                << " cur_real=" << cur_real.DebugString();
      return false;
    }
  }

  for (const auto& subprog : subprograms_) {
    FileId file_id(subprog.name);
    if (file_id != subprog.file_id) {
      LOG(INFO) << "subprogram is not matched:"
                << " local_compiler_path=" << local_compiler_path
                << " subprogram=" << subprog.name
                << " subprogram_file_id=" << subprog.file_id.DebugString()
                << " file_id=" << file_id.DebugString();
      return false;
    }
  }

  return true;
}

bool CompilerInfo::UpdateFileIdIfHashMatch(
    unordered_map<string, string>* sha256_cache) {
  // Checks real compiler hash and subprogram hash.
  // If they are all matched, we update FileId.

  string local_hash;
  if (!GetHashFromCacheOrFile(abs_local_compiler_path(),
                              &local_hash,
                              sha256_cache)) {
    LOG(WARNING) << "calculating local compiler hash failed: "
                 << "path=" << local_compiler_path();
    return false;
  }
  if (local_hash != local_compiler_hash()) {
    LOG(INFO) << "local compiler hash didn't match:"
              << " path=" << local_compiler_path()
              << " prev=" << local_compiler_hash()
              << " current=" << local_hash;
    return false;
  }

  string real_hash;
  if (!GetHashFromCacheOrFile(real_compiler_path(), &real_hash, sha256_cache)) {
    LOG(WARNING) << "calculating real compiler hash failed: "
                 << "path=" << real_compiler_path();
    return false;
  }
  if (real_hash != real_compiler_hash()) {
    LOG(INFO) << "real compiler hash didn't match:"
              << " path=" << real_compiler_path()
              << " prev=" << real_compiler_hash()
              << " current=" << real_hash;
    return false;
  }

  for (const auto& subprog : subprograms_) {
    string subprogram_hash;
    if (!GetHashFromCacheOrFile(subprog.name, &subprogram_hash, sha256_cache)) {
      LOG(WARNING) << "calculating subprogram hash failed: "
                   << "name=" << subprog.name;
      return false;
    }
    if (subprogram_hash != subprog.hash) {
      LOG(INFO) << "subprogram hash didn't match:"
                << " path=" << real_compiler_path()
                << " subprogram=" << subprog.name
                << " prev=" << subprog.hash
                << " current=" << subprogram_hash;
      return false;
    }
  }

  if (subprograms().size() !=
      static_cast<size_t>(data_->subprograms().size())) {
    LOG(ERROR) << "CompilerInfo subprograms and data subprograms size differs: "
               << " Inconsistent state: " << data_->real_compiler_path();
    return false;
  }

  for (size_t i = 0; i < subprograms_.size(); ++i) {
    const auto& subprog = subprograms_[i];
    const auto& data_subprog = data_->subprograms(i);
    if (subprog.name != data_subprog.name()) {
      LOG(ERROR) << "CompilerInfo subprogram and its data subprograms"
                 << " is inconsistent: compiler=" << data_->real_compiler_path()
                 << " inconsistent subprogram: "
                 << subprog.name << " != " << data_subprog.name();
      return false;
    }
  }

  // OK. all hash matched. Let's update FileId.

  FileId cur_local(local_compiler_path());
  if (cur_local != local_compiler_id_) {
    LOG(INFO) << "local_compiler_id_ is updated:"
              << " old=" << local_compiler_id_.DebugString()
              << " new=" << cur_local.DebugString();
    local_compiler_id_ = cur_local;
    SetFileIdToData(cur_local, data_->mutable_local_compiler_id());
  }

  // When |local_compiler_path| == |real_compiler_path|,
  // local_compiler_id and real_compiler_id should be the same.
  // Otherwise, we take FileId for real_compiler_path().
  FileId cur_real(cur_local);
  if (local_compiler_path() != real_compiler_path()) {
    cur_real = FileId(real_compiler_path());
  }
  if (cur_real != real_compiler_id_) {
    LOG(INFO) << "real_compiler_id_ is updated:"
              << " old=" << real_compiler_id_.DebugString()
              << " new=" << cur_real.DebugString();
    real_compiler_id_ = cur_real;
    SetFileIdToData(cur_real, data_->mutable_real_compiler_id());
  }

  for (size_t i = 0; i < subprograms_.size(); ++i) {
    auto& subprog = subprograms_[i];
    auto* data_subprog = data_->mutable_subprograms(i);

    FileId file_id(subprog.name);
    if (file_id != subprog.file_id) {
      LOG(INFO) << "subprogram id is updated:"
                << " name=" << subprog.name
                << " old=" << subprog.file_id.DebugString()
                << " new=" << file_id.DebugString();
      subprog.file_id = file_id;
      SetFileIdToData(file_id, data_subprog->mutable_file_id());
    }
  }

  return true;
}

bool CompilerInfo::IsSystemInclude(const string& filepath) const {
  for (const auto& path : cxx_system_include_paths_) {
    if (HasPrefixDir(filepath, path))
      return true;
  }
  for (const auto& path : system_include_paths_) {
    if (HasPrefixDir(filepath, path))
      return true;
  }
  for (const auto& path : system_framework_paths_) {
    if (HasPrefixDir(filepath, path))
      return true;
  }
  return false;
}

bool CompilerInfo::IsCwdRelative(const string& cwd) const {
  if (HasPrefixDir(data_->real_compiler_path(), cwd)) {
    VLOG(1) << "real_compiler_path is cwd relative:"
            << data_->real_compiler_path()
            << " @" << cwd;
    return true;
  }
  for (size_t i = 0; i < quote_include_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(quote_include_paths_[i]) ||
        HasPrefixDir(quote_include_paths_[i], cwd)) {
      VLOG(1) << "quote_include_path[" << i << "] is cwd relative:"
              << quote_include_paths_[i] << " @" << cwd;
      return true;
    }
  }
  for (size_t i = 0; i < cxx_system_include_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(cxx_system_include_paths_[i]) ||
        HasPrefixDir(cxx_system_include_paths_[i], cwd)) {
      VLOG(1) << "cxx_system_include_path[" << i << "] is cwd relative:"
              << cxx_system_include_paths_[i] << " @" << cwd;
      return true;
    }
  }
  for (size_t i = 0; i < system_include_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(system_include_paths_[i]) ||
        HasPrefixDir(system_include_paths_[i], cwd)) {
      VLOG(1) << "system_include_path[" << i << "] is cwd relative:"
              << system_include_paths_[i] << " @" << cwd;
      return true;
    }
  }
  for (size_t i = 0; i < system_framework_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(system_framework_paths_[i]) ||
        HasPrefixDir(system_framework_paths_[i], cwd)) {
      VLOG(1) << "system_framework_path[" << i << "] is cwd relative:"
              << system_framework_paths_[i] << " @" << cwd;
      return true;
    }
  }
  if (data_->predefined_macros().find(cwd) != string::npos) {
    VLOG(1) << "predefined macros contains cwd " << cwd;
    return true;
  }
  for (size_t i = 0; i < subprograms_.size(); ++i) {
    const string& name = subprograms_[i].name;
    if (HasPrefixDir(name, cwd)) {
      VLOG(1) << "subprograms[" << i << "] is cwd relative: "
              << name << " @" << cwd;
      return true;
    }
  }
  return false;
}

string CompilerInfo::abs_local_compiler_path() const {
  return file::JoinPathRespectAbsolute(
      data_->cwd(), data_->local_compiler_path());
}

const string& CompilerInfo::request_compiler_hash() const {
  if (CompilerFlags::IsPNaClClangCommand(data_->local_compiler_path())) {
    return data_->local_compiler_hash();
  }
  return data_->hash();
}

void CompilerInfo::Init() {
  CHECK(data_.get());
  GetFileIdFromData(data_->local_compiler_id(), &local_compiler_id_);
  GetFileIdFromData(data_->real_compiler_id(), &real_compiler_id_);

  for (const auto& p : data_->quote_include_paths()) {
    quote_include_paths_.push_back(p);
  }
  for (const auto& p : data_->cxx_system_include_paths()) {
    cxx_system_include_paths_.push_back(p);
  }
  for (const auto& p : data_->system_include_paths()) {
    system_include_paths_.push_back(p);
  }
  for (const auto& p : data_->system_framework_paths()) {
    system_framework_paths_.push_back(p);
  }

  for (const auto& m : data_->supported_predefined_macros()) {
    if (!supported_predefined_macros_.insert(make_pair(m, false)).second) {
      LOG(WARNING) << "duplicated predefined_macro: "
                   << " real_compiler_path=" << data_->real_compiler_path()
                   << " macro=" << m;
    }
  }
  for (const auto& m : data_->hidden_predefined_macros()) {
    if (!supported_predefined_macros_.insert(make_pair(m, true)).second) {
      LOG(WARNING) << "duplicated predefined_macro: "
                   << " real_compiler_path=" << data_->real_compiler_path()
                   << " macro=" << m;
    }
  }
  for (const auto& p : data_->has_feature()) {
    has_feature_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->has_extension()) {
    has_extension_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->has_attribute()) {
    has_attribute_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->has_cpp_attribute()) {
    has_cpp_attribute_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->has_declspec_attribute()) {
    has_declspec_attribute_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->has_builtin()) {
    has_builtin_.insert(make_pair(p.key(), p.value()));
  }

  for (const auto& f : data_->additional_flags()) {
    additional_flags_.push_back(f);
  }

  for (const auto& data : data_->subprograms()) {
    SubprogramInfo s;
    SubprogramInfo::FromData(data, &s);
    subprograms_.push_back(s);
  }
}

time_t CompilerInfo::last_used_at() const {
  AUTO_SHARED_LOCK(lock, &last_used_at_mu_);
  return data_->last_used_at();
}

void CompilerInfo::set_last_used_at(time_t t) {
  AUTO_EXCLUSIVE_LOCK(lock, &last_used_at_mu_);
  data_->set_last_used_at(t);
}

CompilerInfoState::CompilerInfoState(std::unique_ptr<CompilerInfoData> data)
    : compiler_info_(std::move(data)),
      refcnt_(0),
      disabled_(false),
      used_(0) {
  LOG(INFO) << "New CompilerInfoState " << this;
  if (!compiler_info_.found() && !compiler_info_.HasError()) {
    CompilerInfoBuilder::AddErrorMessage("compiler not found",
                                         compiler_info_.get());
  }
}

CompilerInfoState::~CompilerInfoState() {}


void CompilerInfoState::Ref() {
  AUTOLOCK(lock, &mu_);
  refcnt_++;
}

void CompilerInfoState::Deref() {
  int refcnt;
  {
    AUTOLOCK(lock, &mu_);
    refcnt_--;
    refcnt = refcnt_;
  }
  if (refcnt == 0) {
    LOG(INFO) << "Delete CompilerInfoState " << this;
    delete this;
  }
}

int CompilerInfoState::refcnt() const {
  AUTOLOCK(lock, &mu_);
  return refcnt_;
}

bool CompilerInfoState::disabled() const {
  AUTOLOCK(lock, &mu_);
  return disabled_;
}

string CompilerInfoState::GetDisabledReason() const {
  AUTOLOCK(lock, &mu_);
  return disabled_reason_;
}

void CompilerInfoState::SetDisabled(bool disabled,
                                    const string& disabled_reason) {
  AUTOLOCK(lock, &mu_);
  LOG(INFO) << "CompilerInfoState " << this << " disabled=" << disabled
            << " reason=" << disabled_reason;
  disabled_ = true;
  disabled_reason_ = disabled_reason;
}

void CompilerInfoState::Use(const string& local_compiler_path,
                            const CompilerFlags& flags) {
  {
    AUTOLOCK(lock, &mu_);
    if (used_++ > 0)
      return;
  }

  // CompilerInfo::DebugString() could be too large for glog.
  // glog message size is 30000 by default.
  // https://github.com/google/glog/blob/bf766fac4f828c81556499d7c16d53cc871d8bd2/src/logging.cc#L335
  // So, split info log at max 20000.
  //
  // TODO: It might be good to introduce a new compact printer for
  // CompilerInfo. I tried implementing it with
  // google::protobuf::TextFormat::Printer, but it is hardcoding ':'
  // (key: value), so I gave it up to make a neat Printer with
  // TextFormat::Printer.
  string info = compiler_info_.DebugString();
  StringPiece piece(info);

  LOG(INFO) << "compiler_info_state=" << this
            << " path=" << local_compiler_path
            << ": flags=" << flags.compiler_info_flags()
            << ": info=" << piece.substr(0, std::min(static_cast<size_t>(20000),
                                                     piece.size()));

  size_t begin_pos = 20000;
  while (begin_pos < piece.size()) {
    size_t len = std::min(static_cast<size_t>(20000),
                          piece.size() - begin_pos);
    LOG(INFO) << "info continued:"
              << " compiler_info_state=" << this
              << " info(continued)=" << piece.substr(begin_pos, len);
    begin_pos += len;
  }
}

int CompilerInfoState::used() const {
  AUTOLOCK(lock, &mu_);
  return used_;
}

void CompilerInfoState::UpdateLastUsedAt() {
  compiler_info_.set_last_used_at(time(nullptr));
}

ScopedCompilerInfoState::ScopedCompilerInfoState(CompilerInfoState* state)
    : state_(state) {
  if (state_ != nullptr)
    state_->Ref();
}

ScopedCompilerInfoState::~ScopedCompilerInfoState() {
  if (state_ != nullptr)
    state_->Deref();
}

void ScopedCompilerInfoState::reset(CompilerInfoState* state) {
  if (state != nullptr)
    state->Ref();
  if (state_ != nullptr)
    state_->Deref();
  state_ = state;
}

void ScopedCompilerInfoState::swap(ScopedCompilerInfoState* other) {
  CompilerInfoState* other_state = other->state_;
  other->state_ = state_;
  state_ = other_state;
}

bool ScopedCompilerInfoState::disabled() const {
  if (state_ == nullptr)
    return true;

  return state_->disabled();
}

string ScopedCompilerInfoState::GetDisabledReason() const {
  if (state_ == nullptr)
    return string();

  return state_->GetDisabledReason();
}

}  // namespace devtools_goma
