// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gcc_compiler_info_builder.h"

#include "absl/strings/match.h"
#include "autolock_timer.h"
#include "clang_compiler_info_builder_helper.h"
#include "counterz.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "path.h"
#include "path_resolver.h"
#include "util.h"

#ifdef _WIN32
#include "posix_helper_win.h"
#endif  // _WIN32

namespace devtools_goma {

namespace {

class GetClangPluginPath : public FlagParser::Callback {
 public:
  explicit GetClangPluginPath(std::vector<string>* subprograms)
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

bool AddSubprogramInfo(
    const string& path,
    google::protobuf::RepeatedPtrField<CompilerInfoData::SubprogramInfo>* ss) {
  CompilerInfoData::SubprogramInfo* s = ss->Add();
  if (!CxxCompilerInfoBuilder::SubprogramInfoFromPath(path, s)) {
    ss->RemoveLast();
    return false;
  }
  return true;
}

// Execute GCC and get the string output for GCC version
bool GetGccVersion(const string& bare_gcc,
                   const std::vector<string>& compiler_info_envs,
                   const string& cwd,
                   string* version) {
  std::vector<string> argv;
  argv.push_back(bare_gcc);
  argv.push_back("-dumpversion");
  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");
  int32_t status = 0;
  string dumpversion_output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(dumpversion)");
    dumpversion_output = ReadCommandOutput(bare_gcc, argv, env, cwd,
                                           MERGE_STDOUT_STDERR, &status);
  }

  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " bare_gcc=" << bare_gcc << " status=" << status
               << " argv=" << argv << " env=" << env << " cwd=" << cwd
               << " dumpversion_output=" << dumpversion_output;
    return false;
  }

  argv[1] = "--version";
  string version_output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(version)");
    version_output = ReadCommandOutput(bare_gcc, argv, env, cwd,
                                       MERGE_STDOUT_STDERR, &status);
  }
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " bare_gcc=" << bare_gcc << " status=" << status
               << " argv=" << argv << " env=" << env << " cwd=" << cwd
               << " version_output=" << version_output;
    return false;
  }

  if (dumpversion_output.empty() || version_output.empty()) {
    LOG(ERROR) << "dumpversion_output or version_output is empty."
               << " bare_gcc=" << bare_gcc << " status=" << status
               << " argv=" << argv << " env=" << env << " cwd=" << cwd
               << " dumpversion_output=" << dumpversion_output
               << " version_output=" << version_output;
    return false;
  }
  *version = GetCxxCompilerVersionFromCommandOutputs(
      bare_gcc, dumpversion_output, version_output);
  return true;
}

// Execute GCC and get the string output for GCC target architecture
// This target is used to pick the same compiler in the backends, so
// we don't need to use compiler_info_flags here.
bool GetGccTarget(const string& bare_gcc,
                  const std::vector<string>& compiler_info_envs,
                  const string& cwd,
                  string* target) {
  std::vector<string> argv;
  argv.push_back(bare_gcc);
  argv.push_back("-dumpmachine");
  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");
  int32_t status = 0;
  string gcc_output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(dumpmachine)");
    gcc_output = ReadCommandOutput(bare_gcc, argv, env, cwd,
                                   MERGE_STDOUT_STDERR, &status);
  }
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " bare_gcc=" << bare_gcc << " status=" << status
               << " argv=" << argv << " env=" << env << " cwd=" << cwd
               << " gcc_output=" << gcc_output;
    return false;
  }
  *target = GetFirstLine(gcc_output);
  return !target->empty();
}

bool IsExecutable(const string& cwd, const string& path) {
  const string abs_path = file::JoinPathRespectAbsolute(cwd, path);
  return access(abs_path.c_str(), X_OK) == 0;
}

#if defined(__linux__) || defined(__MACH__)
string GetRealClangPath(const string& normal_gcc_path,
                        const string& cwd,
                        const std::vector<string>& envs) {
  std::vector<string> argv;
  argv.push_back(normal_gcc_path);
  argv.push_back("-xc");
  argv.push_back("-v");
  argv.push_back("-E");
  argv.push_back("/dev/null");
  int32_t status = 0;
  string v_output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(-xc -v)");
    v_output = ReadCommandOutput(normal_gcc_path, argv, envs, cwd,
                                 MERGE_STDOUT_STDERR, &status);
  }
  LOG_IF(ERROR, status != 0)
      << "ReadCommandOutput exited with non zero status code."
      << " normal_gcc_path=" << normal_gcc_path << " status=" << status
      << " argv=" << argv << " envs=" << envs << " cwd=" << cwd
      << " v_output=" << v_output;
  const string clang_path =
      ClangCompilerInfoBuilderHelper::ParseRealClangPath(v_output);
  if (!clang_path.empty() && IsExecutable(cwd, clang_path)) {
    return clang_path;
  }
  return string();
}
#endif

}  // anonymous namespace

void GCCCompilerInfoBuilder::SetTypeSpecificCompilerInfo(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  // Some compilers uses wrapper script to set build target, and in such a
  // situation, build target could be different.
  // To make goma backend use proper wrapper script, or set proper -target,
  // we should need to use local_compiler_path instead of real path.
  bool has_version = GetGccVersion(abs_local_compiler_path, compiler_info_envs,
                                   flags.cwd(), data->mutable_version());
  bool has_target = GetGccTarget(abs_local_compiler_path, compiler_info_envs,
                                 flags.cwd(), data->mutable_target());


  const GCCFlags& gcc_flags = static_cast<const GCCFlags&>(flags);

  // If input is LLVM IR, we assume it ThinLTO backend phase.
  // The phase should not use system include paths, predefined macro and
  // features.
  //
  // See also:
  // http://blog.llvm.org/2016/06/thinlto-scalable-and-incremental-lto.html
  const bool is_input_ir = gcc_flags.lang() == "ir";

  // TODO: As we have -x flags in compiler_info,
  //               include_processor don't need to have 2 kinds of
  //               system include paths (C and C++).
  //               However, we still need them because backend
  //               should set them using different ways
  //               (-isystem and CPLUS_INCLUDE_PATH).
  //               Once b/5218687 is fixed, we should
  //               be able to eliminate cxx_system_include_paths.
  if (!is_input_ir &&
      !ClangCompilerInfoBuilderHelper::SetBasicCompilerInfo(
          local_compiler_path, gcc_flags.compiler_info_flags(),
          compiler_info_envs, gcc_flags.cwd(), "-x" + flags.lang(),
          gcc_flags.resource_dir(), gcc_flags.is_cplusplus(),
          gcc_flags.has_nostdinc(), data)) {
    DCHECK(data->has_error_message());
    // If error occurred in SetBasicCompilerInfo, we do not need to
    // continue.
    return;
  }

  if (!has_version) {
    AddErrorMessage("Failed to get version for " + data->real_compiler_path(),
                    data);
    LOG(ERROR) << data->error_message();
    return;
  }
  if (!has_target) {
    AddErrorMessage("Failed to get target for " + data->real_compiler_path(),
                    data);
    LOG(ERROR) << data->error_message();
    return;
  }

  if (!GetExtraSubprograms(local_compiler_path, gcc_flags, compiler_info_envs,
                           data)) {
    std::ostringstream ss;
    ss << "Failed to get subprograms for " << data->real_compiler_path();
    AddErrorMessage(ss.str(), data);
    LOG(ERROR) << data->error_message();
    return;
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
    for (const auto& m : data->cxx().supported_predefined_macros()) {
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
        (data->cxx().predefined_macros().find("__has_include__") !=
         string::npos)) {
      data->mutable_cxx()->add_hidden_predefined_macros("__has_include__");
    }
    if (has_include_next && !has_include_next__ &&
        (data->cxx().predefined_macros().find("__has_include_next__") !=
         string::npos)) {
      data->mutable_cxx()->add_hidden_predefined_macros("__has_include_next__");
    }
  }

  // Experimental. Add compiler resource.
  // TODO: We also need *.so, too.
  // For chromium clang, we need *.so if sanitizer is used.
  // If sanitizer is not used, clang works in normal case.
  // TODO: Support the case local compiler and real compiler are
  // different.
  CompilerInfoData::ResourceInfo r;
  if (!CompilerInfoBuilder::ResourceInfoFromPath(
          flags.cwd(), local_compiler_path, CompilerInfoData::EXECUTABLE_BINARY,
          &r)) {
    AddErrorMessage("failed to get resource info for " + local_compiler_path,
                    data);
    return;
  }
  *data->add_resource() = std::move(r);
}

void GCCCompilerInfoBuilder::SetCompilerPath(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  data->set_local_compiler_path(local_compiler_path);
  data->set_real_compiler_path(GetRealCompilerPath(
      local_compiler_path, flags.cwd(), compiler_info_envs));
}

string GCCCompilerInfoBuilder::GetCompilerName(
    const CompilerInfoData& data) const {
  absl::string_view base = file::Basename(data.local_compiler_path());
  if (base != "cc" && base != "c++") {
    // We can simply use local_compiler_path for judging compiler name
    // if basename is not "cc" or "c++".
    // See also b/13107706
    return GCCFlags::GetCompilerName(data.local_compiler_path());
  }

  if (!GCCFlags::IsClangCommand(data.real_compiler_path())) {
    return GCCFlags::GetCompilerName(data.real_compiler_path());
  }

  // clang++ is usually symlink to clang, and real compiler path is
  // usually be clang.  It does not usually reflect what we expect as a
  // compiler name.
  string real_name = GCCFlags::GetCompilerName(data.real_compiler_path());
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

/* static */
bool GCCCompilerInfoBuilder::GetExtraSubprograms(
    const string& normal_gcc_path,
    const GCCFlags& gcc_flags,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* compiler_info) {
  // TODO: support linker subprograms on linking.
  std::vector<string> clang_plugins;
  std::vector<string> B_options;
  bool no_integrated_as = false;
  std::set<string> known_subprograms;
  ParseSubprogramFlags(normal_gcc_path, gcc_flags, &clang_plugins, &B_options,
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
                 << " compiler_info_flags=" << gcc_flags.compiler_info_flags();
      return false;
    }
  }

  std::vector<string> subprogram_paths;
  if (!CxxCompilerInfoBuilder::GetSubprograms(
          normal_gcc_path, gcc_flags.lang(), gcc_flags.compiler_info_flags(),
          compiler_info_envs, gcc_flags.cwd(), no_integrated_as,
          &subprogram_paths)) {
    LOG(ERROR) << "failed to get subprograms.";
    return false;
  }
  if (no_integrated_as && !HasAsPath(subprogram_paths)) {
    LOG(ERROR) << "no_integrated_as is set but we cannot find as.";
    return false;
  }
  for (const auto& path : subprogram_paths) {
    bool may_register = false;
    if (no_integrated_as && absl::EndsWith(path, "as")) {
      may_register = true;
    } else {
      // List only subprograms under -B path for backward compatibility.
      // See b/63082235
      for (const string& b : B_options) {
        if (absl::StartsWith(path, b)) {
          may_register = true;
          break;
        }
      }
    }
    if (!may_register) {
      LOG(INFO) << "showed up as subprogram but not sent for"
                << " backword compatibility."
                << " path=" << path << " normal_gcc_path=" << normal_gcc_path
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
                 << " compiler_info_flags=" << gcc_flags.compiler_info_flags();
      return false;
    }
  }
  return true;
}

/* static */
void GCCCompilerInfoBuilder::ParseSubprogramFlags(
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
  flag_parser.AddBoolFlag("no-integrated-as")->SetSeenOutput(no_integrated_as);
  flag_parser.AddBoolFlag("fno-integrated-as")->SetSeenOutput(no_integrated_as);

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

// static
bool GCCCompilerInfoBuilder::HasAsPath(
    const std::vector<string>& subprogram_paths) {
  for (const auto& path : subprogram_paths) {
    absl::string_view basename = file::Basename(path);
    if (basename == "as" || absl::EndsWith(basename, "-as")) {
      return true;
    }
  }
  return false;
}

// static
string GCCCompilerInfoBuilder::GetRealCompilerPath(
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
  if (GCCFlags::IsClangCommand(normal_gcc_path)) {
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
    if (IsExecutable(cwd, real_chromeos_clang_path)) {
      return real_chromeos_clang_path;
    }
    return real_path;
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
  string v_output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(-v)");
    v_output = ReadCommandOutput(normal_gcc_path, argv, envs, cwd,
                                 MERGE_STDOUT_STDERR, &status);
  }
  LOG_IF(ERROR, status != 0)
      << "ReadCommandOutput exited with non zero status code."
      << " normal_gcc_path=" << normal_gcc_path << " status=" << status
      << " argv=" << argv << " envs=" << envs << " cwd=" << cwd
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
  if (IsExecutable(cwd, real_gcc_path)) {
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
  LOG(INFO) << "The command seems not clang. Use it as-is: " << normal_gcc_path;
  return normal_gcc_path;
#endif
#ifdef _WIN32
  // For Windows nacl-{gcc,g++}.
  // The real binary is ../libexec/nacl-{gcc,g++}.exe.  Binaries under
  // the bin directory are just wrappers to them.
  if (GCCFlags::IsNaClGCCCommand(normal_gcc_path)) {
    const string& candidate_path = file::JoinPath(
        ClangCompilerInfoBuilderHelper::GetNaClToolchainRoot(normal_gcc_path),
        file::JoinPath("libexec", file::Basename(normal_gcc_path)));
    if (IsExecutable(cwd, candidate_path)) {
      return candidate_path;
    }
    LOG(ERROR) << "cannot find nacl-gcc's real compiler path."
               << " normal_gcc_path=" << normal_gcc_path
               << " cwd=" << cwd
               << " candidate_path=" << candidate_path;
  }
  return normal_gcc_path;
#endif
}

}  // namespace devtools_goma
