// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "java/java_compiler_type_specific.h"

#include "glog/logging.h"
#include "java/jar_parser.h"
#include "java_flags.h"

namespace devtools_goma {

std::unique_ptr<CompilerInfoData>
JavaCompilerTypeSpecific::BuildCompilerInfoData(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  return compiler_info_builder_.FillFromCompilerOutputs(
      flags, local_compiler_path, compiler_info_envs);
}

CompilerTypeSpecific::IncludeProcessorResult
JavaCompilerTypeSpecific::RunIncludeProcessor(
    const string& trace_id,
    const CompilerFlags& compiler_flags,
    const CompilerInfo& compiler_info,
    const CommandSpec& command_spec,
    FileStatCache* file_stat_cache) {
  DCHECK_EQ(CompilerFlagType::Java, compiler_flags.type());

  LOG(ERROR) << "Java type does not have any include processor";
  return IncludeProcessorResult::ErrorToLog(
      "Java type does not have any include processor");
}

// ----------------------------------------------------------------------

std::unique_ptr<CompilerInfoData>
JavacCompilerTypeSpecific::BuildCompilerInfoData(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  return compiler_info_builder_.FillFromCompilerOutputs(
      flags, local_compiler_path, compiler_info_envs);
}

CompilerTypeSpecific::IncludeProcessorResult
JavacCompilerTypeSpecific::RunIncludeProcessor(
    const string& trace_id,
    const CompilerFlags& compiler_flags,
    const CompilerInfo& compiler_info,
    const CommandSpec& command_spec,
    FileStatCache* file_stat_cache) {
  DCHECK_EQ(CompilerFlagType::Javac, compiler_flags.type());

  std::set<string> required_files;
  JarParser jar_parser;
  jar_parser.GetJarFiles(
      static_cast<const JavacFlags&>(compiler_flags).jar_files(),
      compiler_flags.cwd(), &required_files);
  return IncludeProcessorResult::Ok(std::move(required_files));
}

}  // namespace devtools_goma
