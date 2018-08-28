// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_TYPE_SPECIFIC_H_

#include "compiler_type_specific.h"
#include "java/java_compiler_info_builder.h"

namespace devtools_goma {

class JavaCompilerTypeSpecific : public CompilerTypeSpecific {
 public:
  JavaCompilerTypeSpecific(const JavaCompilerTypeSpecific&) = delete;
  void operator=(const JavaCompilerTypeSpecific&) = delete;

  std::unique_ptr<CompilerInfoData> BuildCompilerInfoData(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs) override;

  bool SupportsDepsCache() const override { return false; }

  IncludeProcessorResult RunIncludeProcessor(
      const string& trace_id,
      const CompilerFlags& compiler_flags,
      const CompilerInfo& compiler_info,
      const CommandSpec& command_spec,
      FileStatCache* file_stat_cache) override;

 private:
  JavaCompilerTypeSpecific() = default;

  JavaCompilerInfoBuilder compiler_info_builder_;

  friend class CompilerTypeSpecificCollection;
};

class JavacCompilerTypeSpecific : public CompilerTypeSpecific {
 public:
  JavacCompilerTypeSpecific(const JavacCompilerTypeSpecific&) = delete;
  void operator=(const JavacCompilerTypeSpecific&) = delete;

  std::unique_ptr<CompilerInfoData> BuildCompilerInfoData(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs) override;

  bool SupportsDepsCache() const override { return false; }

  IncludeProcessorResult RunIncludeProcessor(
      const string& trace_id,
      const CompilerFlags& compiler_flags,
      const CompilerInfo& compiler_info,
      const CommandSpec& command_spec,
      FileStatCache* file_stat_cache) override;

 private:
  JavacCompilerTypeSpecific() = default;

  JavacCompilerInfoBuilder compiler_info_builder_;

  friend class CompilerTypeSpecificCollection;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_TYPE_SPECIFIC_H_
