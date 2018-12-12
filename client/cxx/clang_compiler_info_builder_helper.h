// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_CLANG_COMPILER_INFO_BUILDER_HELPER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_CLANG_COMPILER_INFO_BUILDER_HELPER_H_

#include <string>

#include "absl/strings/string_view.h"
#include "compiler_specific.h"
#include "cxx_compiler_info_builder.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

// ClangCompilerInfoBuilderHelper is a collection of methods related to
// gcc and clang.
//
// This will be used from GCCCompilerInfoBuilder, VCCompilerInfoBuilder,
// and ClangTidyCompilerInfoBuilder.
//
// For methods which are gcc only, use GccCompilerInfoBuilder.
// For Methods that can be used for clang, implement it here.
class ClangCompilerInfoBuilderHelper {
 public:
  using ResourceList = std::pair<string, CompilerInfoData::ResourceType>;
  enum class ParseStatus {
    kSuccess,
    kFail,
    kNotParsed,
  };
  typedef std::pair<const char* const*, size_t> FeatureList;

  // Sets the compiler resource directory. asan_blacklist.txt etc. are
  // located in this directory.
  // Returns true if succeeded.
  static bool GetResourceDir(const string& c_display_output,
                             CompilerInfoData* compiler_info);

  // Parse |display_output| to get list of additional inputs.
  static ParseStatus ParseResourceOutput(const string& compiler_path,
                                         const string& cwd,
                                         const string& display_output,
                                         std::vector<ResourceList>* paths);

  // Parses "-xc -v -E /dev/null" output and returns real clang path.
  static string ParseRealClangPath(absl::string_view v_out);

  // Parses output of clang / clang-cl -### result to get
  // |version| and |target|.
  static bool ParseClangVersionTarget(const string& sharp_output,
                                      string* version,
                                      string* target);

  static bool GetPredefinedMacros(
      const string& normal_compiler_path,
      const std::vector<string>& compiler_info_flags,
      const std::vector<string>& compiler_info_envs,
      const string& cwd,
      const string& lang_flag,
      CompilerInfoData* compiler_info);

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

  static bool SetBasicCompilerInfo(
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_flags,
      const std::vector<string>& compiler_info_envs,
      const string& cwd,
      const string& lang_flag,
      const string& resource_dir,
      bool is_cplusplus,
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

  // helper methods.
  // Parses output of "gcc -x <lang> -v -E /dev/null -o /dev/null", and
  // extracts |qpaths| (for #include "..."),
  // |paths| (for #include <...>) and |framework_paths|.
  static bool SplitGccIncludeOutput(const string& gcc_v_output,
                                    std::vector<string>* qpaths,
                                    std::vector<string>* paths,
                                    std::vector<string>* framework_paths);

  // Set up system include_paths to be sent to goma backend via ExecReq.
  // To make the compile deterministic, we sometimes need to use relative
  // path system include paths, and UpdateIncludePaths automatically
  // converts the paths.
  static void UpdateIncludePaths(
      const std::vector<string>& paths,
      google::protobuf::RepeatedPtrField<string>* include_paths);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_CLANG_COMPILER_INFO_BUILDER_HELPER_H_
