// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_H_

#include <sys/types.h>
#include <time.h>

#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/strings/string_view.h"
#include "compiler_specific.h"
#include "file_id.h"
#include "google/protobuf/repeated_field.h"
#include "gtest/gtest_prod.h"
#include "lockhelper.h"
#include "predefined_macros.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

class CompilerFlags;
class GCCFlags;

// CompilerInfoBuilder provides methods to construct CompilerInfoData.
//
//   CompielrInfoBuilder cib;
//   std::unique_ptr<CompilerInfoData> data(
//      cib.FillFromCompilerOutputs(....));
//   CompilerInfo compiler_info(std::move(data));
class CompilerInfoBuilder {
 public:
  typedef std::pair<const char* const*, size_t> FeatureList;

  CompilerInfoBuilder() {}
  ~CompilerInfoBuilder() {}

  // Creates new CompilerInfoData* from compiler outputs.
  // if found is true and error_message in it is empty,
  // it successfully gets compiler info.
  // if found is true and error_message in it is not empty,
  // it finds local compiler but failed to get some information, such as
  // system include paths.
  // if found is false if it fails to find local compiler.
  // Caller should take ownership of returned CompilerInfoData.
  std::unique_ptr<CompilerInfoData> FillFromCompilerOutputs(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs);

  // helper methods.
  // Parses output of "gcc -x <lang> -v -E /dev/null -o /dev/null", and
  // extracts |qpaths| (for #include "..."),
  // |paths| (for #include <...>) and |framework_paths|.
  static bool SplitGccIncludeOutput(
      const string& gcc_v_output,
      std::vector<string>* qpaths,
      std::vector<string>* paths,
      std::vector<string>* framework_paths);

  // Parses output of clang feature macros.
  static bool ParseFeatures(const string& feature_output,
                            FeatureList object_macros,
                            FeatureList function_macros,
                            FeatureList feature,
                            FeatureList extension,
                            FeatureList attribute,
                            FeatureList cpp_attribute,
                            FeatureList declspec_attribute,
                            FeatureList builtins,
                            CompilerInfoData* compiler_info);

  static bool GetPredefinedFeaturesAndExtensions(
    const string& normal_compiler_path,
    const string& lang_flag,
    const std::vector<string>& compiler_info_flags,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    CompilerInfoData* compiler_info);

  static bool GetAdditionalFlags(
      const string& gxx_output, std::vector<string>* flags);

  // Sets the compiler resource directory. asan_blacklist.txt etc. are
  // located in this directory.
  // Returns true if succeeded.
  static bool GetResourceDir(const string& c_display_output,
                             CompilerInfoData* compiler_info);

  // Returns false if GetExtraSubprograms failed to get subprogram info
  // while a subprogram exists.
  static bool GetExtraSubprograms(const string& normal_gcc_path,
                                  const GCCFlags& flags,
                                  const std::vector<string>& compiler_info_envs,
                                  CompilerInfoData* compiler_info);

  // Parses compile flags for subprograms, especially clang plugins.
  static void ParseSubprogramFlags(const string& normal_gcc_path,
                                   const GCCFlags& flags,
                                   std::vector<string>* clang_plugins,
                                   std::vector<string>* B_options,
                                   bool* no_integrated_as);
  // Parse |gcc_output| to get list of subprograms.
  static void ParseGetSubprogramsOutput(const string& gcc_output,
                                        std::vector<string>* paths);

  // Returns true on success, and |subprograms| will have full path of
  // external subprograms or empty vector if not found.
  // Returns false on failure.
  static bool GetSubprograms(
      const string& gcc_path,
      const string& lang,
      const std::vector<string>& compiler_info_flags,
      const std::vector<string>& compiler_info_envs,
      const string& cwd, bool warn_on_empty,
      std::vector<string>* subprograms);

  // Returns true if |subprogram_paths| contain a path for as (assembler).
  static bool HasAsPath(const std::vector<string>& subprogram_paths);

  // Parses "-xc -v -E /dev/null" output and returns real clang path.
  static string ParseRealClangPath(absl::string_view v_out);

  // Get real compiler path.
  // See: go/ma/resources-for-developers/goma-compiler-selection-mechanism
  static string GetRealCompilerPath(const string& normal_gcc_path,
                                    const string& cwd,
                                    const std::vector<string>& envs);
  // Get real subprogram path.
  // See: go/ma/resources-for-developers/goma-compiler-selection-mechanism
  static string GetRealSubprogramPath(const string& subprogram_path);

  // Parses output of "javac", and extracts |version|.
  static bool ParseJavacVersion(const string& vc_logo, string* version);

  // Execute javac and get the string output for javac version
  static bool GetJavacVersion(const string& javac,
                              const std::vector<string>& compiler_info_envs,
                              const string& cwd,
                              string* version);

  // Parses output of "cl.exe", and extracts |version| and |target|.
  static bool ParseVCVersion(
      const string& vc_logo, string* version, string* target);

  // Execute VC and get the string output for VC version
  static bool GetVCVersion(
    const string& cl_exe_path, const std::vector<string>& env,
    const string& cwd,
    string* version, string* target);

  // Parses output of "cl.exe /nologo /Bxvcflags.exe non-exist-file.cpp" (C++)
  // or "cl.exe /nologo /B1vcflags.exe non-exist-file.c" (C),
  // and extracts |include_paths| and |predefined macros| in
  // "#define FOO X\n" format.
  // |predefined_macros| may be NULL (don't capture predefined macros
  // in this case).
  static bool ParseVCOutputString(
      const string& output,
      std::vector<string>* include_paths,
      string* predefined_macros);

  // Parses output of clang / clang-cl -### result to get
  // |version| and |target|.
  static bool ParseClangVersionTarget(const string& sharp_output,
                                      string* version,
                                      string* target);

  // Executes clang-tidy and gets the string output for clang-tidy version.
  static bool GetClangTidyVersionTarget(
      const string& clang_tidy_path,
      const std::vector<string>& compiler_info_envs,
      const string& cwd,
      string* version,
      string* target);
  static bool ParseClangTidyVersionTarget(const string& output,
                                          string* version,
                                          string* target);

  static bool SetBasicCompilerInfo(
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_flags,
      const std::vector<string>& compiler_info_envs,
      const string& cwd,
      const string& lang_flag,
      bool is_cplusplus,
      bool is_clang,
      bool is_clang_tidy,
      bool has_nostdinc,
      CompilerInfoData* compiler_info);

  static bool GetSystemIncludePaths(
      const string& normal_compiler_path,
      const std::vector<string>& compiler_info_flags,
      const std::vector<string>& compiler_info_envs,
      const string& cxx_display_output,
      const string& c_display_output,
      bool is_cplusplus,
      bool has_nostdinc,
      CompilerInfoData* compiler_info);

  static bool GetPredefinedMacros(
      const string& normal_compiler_path,
      const std::vector<string>& compiler_info_flags,
      const std::vector<string>& compiler_info_envs,
      const string& cwd,
      const string& lang_flag,
      CompilerInfoData* compiler_info);

  static bool GetVCDefaultValues(const string& cl_exe_path,
                                 const string& vcflags_path,
                                 const std::vector<string>& compiler_info_flags,
                                 const std::vector<string>& compiler_info_envs,
                                 const string& cwd,
                                 const string& lang,
                                 CompilerInfoData* compiler_info);

  // Set up system include_paths to be sent to goma backend via ExecReq.
  // To make the compile deterministic, we sometimes need to use relative
  // path system include paths, and UpdateIncludePaths automatically
  // converts the paths.
  static void UpdateIncludePaths(
      const std::vector<string>& paths,
      google::protobuf::RepeatedPtrField<string>* include_paths);

  // Adds error message to CompilerInfo. When |failed_at| is not 0,
  // it's also updated.
  static void AddErrorMessage(const std::string& message,
                              CompilerInfoData* compiler_info);
  // Overrides the current error message.
  // if |message| is not empty, |failed_at| must be non-zero positive.
  static void OverrideError(const std::string& message, time_t faile_at,
                            CompilerInfoData* compiler_info);

  static bool SubprogramInfoFromPath(
      const string& path, CompilerInfoData::SubprogramInfo* s);

  void SetHashRewriteRule(const std::map<std::string, std::string>& rule);

  static bool RewriteHashUnlocked(
      const std::map<std::string, std::string>& rule,
      CompilerInfoData* data);

  // Returns compiler name to be used in ExecReq's CompilerSpec.
  // If it fails to identify the compiler name, it returns empty string.
  static string GetCompilerName(const CompilerInfoData& data);

  void Dump(std::ostringstream* ss);

 private:

  ReadWriteLock rwlock_;
  std::map<std::string, std::string> hash_rewrite_rule_;

  DISALLOW_COPY_AND_ASSIGN(CompilerInfoBuilder);
};

// Represent how a compiler is configured.
// Used as const object.
class CompilerInfo {
 public:
  struct SubprogramInfo {
    SubprogramInfo() {}
    static void FromData(const CompilerInfoData::SubprogramInfo& info_data,
                         SubprogramInfo* info);
    static SubprogramInfo FromPath(const string& path);
    bool IsValid() const {
      return file_id.IsValid() && !hash.empty() && !name.empty();
    }
    bool operator==(const SubprogramInfo& rhs) const {
      return name == rhs.name &&
          hash == rhs.hash &&
          file_id == rhs.file_id;
    }
    string DebugString() const;

    string name;
    string hash;
    FileId file_id;
  };

  // Takes ownership of data.
  explicit CompilerInfo(std::unique_ptr<CompilerInfoData> data)
      : data_(std::move(data)) {
    Init();
  }
  ~CompilerInfo() {}

  string DebugString() const;

  // Returns true if |local_compiler_path| is up to date.
  // i.e. FileId of |local_compiler_path| matches |local_compiler_id|.
  bool IsUpToDate(const string& local_compiler_path) const;

  // Updates FileId to the current FileId when hash is matched.
  // Returns false if hash doesn't match.
  bool UpdateFileIdIfHashMatch(
      std::unordered_map<string, string>* sha256_cache);

  // Returns true if CompilerInfo has some error.
  bool HasError() const { return data_->has_error_message(); }

  bool IsSystemInclude(const string& filepath) const;

  bool IsCwdRelative(const string& cwd) const;

  const FileId& local_compiler_id() const { return local_compiler_id_; }
  const string& local_compiler_path() const {
    return data_->local_compiler_path();
  }
  string abs_local_compiler_path() const;
  const string& local_compiler_hash() const {
    return data_->local_compiler_hash();
  }

  const FileId& real_compiler_id() const { return real_compiler_id_; }
  const string& real_compiler_path() const {
    return data_->real_compiler_path();
  }
  const string& real_compiler_hash() const {
    return data_->hash();
  }

  // compiler hash to identify the compiler in backend.
  const string& request_compiler_hash() const;

  // include paths could be relative path from cwd.
  // Also, system include paths could be relative path from toolchain root
  // (Windows NaCl toolchain only).
  // You should file::JoinPathRespectAbsolute with cwd before you use it in
  // include processor.

  // quote dir is valid only if it exists. note quote dir may be cwd relative
  // so it depends on cwd if dir is valid or not.
  const std::vector<string>& quote_include_paths() const {
    return quote_include_paths_;
  }
  const std::vector<string>& cxx_system_include_paths() const {
    return cxx_system_include_paths_;
  }
  const std::vector<string>& system_include_paths() const {
    return system_include_paths_;
  }
  const std::vector<string>& system_framework_paths() const {
    return system_framework_paths_;
  }
  const string& toolchain_root() const {
    return data_->toolchain_root();
  }
  const string& predefined_macros() const {
    return data_->predefined_macros();
  }
  const string& name() const { return data_->name(); }
  bool HasName() const { return data_->has_name(); }

  const string& version() const { return data_->version(); }
  const string& target() const { return data_->target(); }
  const string& lang() const { return data_->lang(); }
  const string& error_message() const { return data_->error_message(); }

  const std::unordered_map<string, bool>& supported_predefined_macros() const {
    return supported_predefined_macros_;
  }
  const std::unordered_map<string, int>& has_feature() const {
    return has_feature_;
  }
  const std::unordered_map<string, int>& has_extension() const {
    return has_extension_;
  }
  const std::unordered_map<string, int>& has_attribute() const {
    return has_attribute_;
  }
  const std::unordered_map<string, int>& has_cpp_attribute() const {
    return has_cpp_attribute_;
  }
  const std::unordered_map<string, int>& has_declspec_attribute() const {
    return has_declspec_attribute_;
  }
  const std::unordered_map<string, int>& has_builtin() const {
    return has_builtin_;
  }
  const std::vector<string>& additional_flags() const {
    return additional_flags_;
  }
  bool HasAdditionalFlags() const { return !additional_flags_.empty(); }
  const std::vector<SubprogramInfo>& subprograms() const {
    return subprograms_;
  }

  time_t failed_at() const { return data_->failed_at(); }

  time_t last_used_at() const;
  void set_last_used_at(time_t t);

  bool found() const { return data_->found(); }

  bool IsSameCompiler(const CompilerInfo& ci) const {
    return data_->target() == ci.data_->target()
        && data_->version() == ci.data_->version()
        && data_->lang() == ci.data_->lang()
        && data_->hash() == ci.data_->hash()
        && data_->real_compiler_path() == ci.data_->real_compiler_path();
  }

  const CompilerInfoData& data() const { return *data_; }

  CompilerInfoData* get() { return data_.get(); }

 private:
  friend class CompilerInfoCacheTest;
  void Init();

  std::unique_ptr<CompilerInfoData> data_;

  FileId local_compiler_id_;
  // Real compiler's FileId if real_compiler_path != local_compiler_path.
  // Otherwise, real_compiler_id is the same as local_compiler_id.
  FileId real_compiler_id_;

  std::vector<string> quote_include_paths_;
  std::vector<string> cxx_system_include_paths_;
  std::vector<string> system_include_paths_;
  std::vector<string> system_framework_paths_;

  // <macro name, hidden>.
  // If it is hidden macro like __has_include__ in GCC 5, hidden is set.
  std::unordered_map<string, bool> supported_predefined_macros_;
  std::unordered_map<string, int> has_feature_;
  std::unordered_map<string, int> has_extension_;
  std::unordered_map<string, int> has_attribute_;
  std::unordered_map<string, int> has_cpp_attribute_;
  std::unordered_map<string, int> has_declspec_attribute_;
  std::unordered_map<string, int> has_builtin_;

  std::vector<string> additional_flags_;

  // A list of subprograms specified by -B flag.
  std::vector<SubprogramInfo> subprograms_;

  mutable ReadWriteLock last_used_at_mu_;

  DISALLOW_COPY_AND_ASSIGN(CompilerInfo);
};

class ScopedCompilerInfoState;

// CompilerInfoState contains CompilerInfo (created from local system) and
// disabled status (updated by response from remote).
// ref counted.
class CompilerInfoState {
 public:
  // Constructor creates with refcnt_==0.
  // Before sharing it, caller should call Ref.
  // Takes ownership of data.
  explicit CompilerInfoState(std::unique_ptr<CompilerInfoData> data);

  const CompilerInfo& info() const { return compiler_info_; }

  // refcnt returns the current reference count.
  // potentially race. when you get the value, actual refcnt may be updated.
  // use be careful.
  int refcnt() const;

  // Returns if it has been disabled (e.g. compiler not found in backend)
  // Potential race (i.e. even if caller gets disabled()==false, it will
  // become true while checking input files or calling rpc), but it might
  // be acceptable.
  bool disabled() const;
  string GetDisabledReason() const;
  void SetDisabled(bool disabled, const string& disabled_reason);

  void Use(const string& local_compiler_path,
           const CompilerFlags& flags);
  int used() const;

  void UpdateLastUsedAt();

 private:
  friend class ScopedCompilerInfoState;
  friend class CompilerInfoCache;
  friend class CompilerInfoCacheTest;
  ~CompilerInfoState();

  void Ref();
  void Deref();

  CompilerInfo compiler_info_;

  mutable Lock mu_;  // protects refcnt_, disabled_, disabled_reason_.
  int refcnt_;
  // When server side does not have the information about this compiler,
  // it's disabled.
  bool disabled_;
  string disabled_reason_;

  int used_;

  DISALLOW_COPY_AND_ASSIGN(CompilerInfoState);
};

// ScopedCompilerInfoState manages lifecycle of CompilerInfoState.
// thread-unsafe.
//
// Initializes
//   ScopedCompilerInfoState cis;
//   cis.FillFromCompilerOutputs(...);
//
// share compiler_info_state with cis:
//   ScopedCompilerInfoState state;
//   state.reset(cis.get());
//
//   ScopedCompilerInfoState state2(cis);
//
// transfer compiler_info_state from cis:
//   ScopedCompilerInfoState state(std::move(cis));
class ScopedCompilerInfoState {
 public:
  ScopedCompilerInfoState() : state_(nullptr) {}
  explicit ScopedCompilerInfoState(CompilerInfoState* state);
  ~ScopedCompilerInfoState();

  ScopedCompilerInfoState(ScopedCompilerInfoState&& state) noexcept
      : state_(std::move(state.state_)) {
    state.state_ = nullptr;
  }

  ScopedCompilerInfoState& operator=(ScopedCompilerInfoState&& other) {
    std::swap(state_, other.state_);
    return *this;
  }

  CompilerInfoState* get() const { return state_; }

  // reset derefs current state and refs given state.
  void reset(CompilerInfoState* state);

  // swap swaps state with other.
  // useful to transfer state from other without modifying refcnt.
  void swap(ScopedCompilerInfoState* other);

  bool disabled() const;
  string GetDisabledReason() const;

 private:
  CompilerInfoState* state_;

  DISALLOW_COPY_AND_ASSIGN(ScopedCompilerInfoState);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_H_
