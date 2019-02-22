// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_GCC_COMPILER_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_GCC_COMPILER_TYPE_SPECIFIC_H_

#include "cxx/cxx_compiler_type_specific.h"
#include "cxx/gcc_compiler_info_builder.h"

namespace devtools_goma {

class GCCCompilerTypeSpecific : public CxxCompilerTypeSpecific {
 public:
  GCCCompilerTypeSpecific(const GCCCompilerTypeSpecific&) = delete;
  void operator=(const GCCCompilerTypeSpecific&) = delete;

  bool RemoteCompileSupported(const string& trace_id,
                              const CompilerFlags& flags,
                              bool verify_output) const override;

  std::unique_ptr<CompilerInfoData> BuildCompilerInfoData(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs) override;

  IncludeProcessorResult RunIncludeProcessor(
      const string& trace_id,
      const CompilerFlags& compiler_flags,
      const CompilerInfo& compiler_info,
      const CommandSpec& command_spec,
      FileStatCache* file_stat_cache) override;

  bool SupportsDepsCache(const CompilerFlags& flags) const override;

  static void SetEnableGchHack(bool enable_gch_hack) {
    enable_gch_hack_ = enable_gch_hack;
  }
  static void SetEnableRemoteLink(bool enable_remote_link) {
    enable_remote_link_ = enable_remote_link;
  }
  static void SetEnableRemoteClangModules(bool enable_remote_clang_modules) {
    enable_remote_clang_modules_ = enable_remote_clang_modules;
  }

 private:
  GCCCompilerTypeSpecific() = default;

  IncludeProcessorResult RunThinLTOImports(const string& trace_id,
                                           const GCCFlags& flags);
  IncludeProcessorResult RunLinkIncludeProcessor(
      const string& trace_id,
      const GCCFlags& flags,
      const CompilerInfo& compiler_info,
      const CommandSpec& command_spec);

  GCCCompilerInfoBuilder compiler_info_builder_;

  friend class CompilerTypeSpecificCollection;

  static bool enable_gch_hack_;
  static bool enable_remote_link_;
  static bool enable_remote_clang_modules_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_GCC_COMPILER_TYPE_SPECIFIC_H_
