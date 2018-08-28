// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gcc_compiler_type_specific.h"

#include "glog/logging.h"
#include "linker/linker_input_processor/linker_input_processor.h"
#include "linker/linker_input_processor/thinlto_import_processor.h"

namespace devtools_goma {

std::unique_ptr<CompilerInfoData>
GCCCompilerTypeSpecific::BuildCompilerInfoData(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  return compiler_info_builder_.FillFromCompilerOutputs(
      flags, local_compiler_path, compiler_info_envs);
}

CompilerTypeSpecific::IncludeProcessorResult
GCCCompilerTypeSpecific::RunIncludeProcessor(
    const string& trace_id,
    const CompilerFlags& compiler_flags,
    const CompilerInfo& compiler_info,
    const CommandSpec& command_spec,
    FileStatCache* file_stat_cache) {
  DCHECK_EQ(CompilerFlagType::Gcc, compiler_flags.type());

  const GCCFlags& flags = static_cast<const GCCFlags&>(compiler_flags);

  if (flags.lang() == "ir") {
    if (flags.thinlto_index().empty()) {
      // No need to to read .imports file. imports is nothing.
      return IncludeProcessorResult::Ok(std::set<string>());
    }

    // Otherwise, run ThinLTOImports.
    return RunThinLTOImports(trace_id, flags);
  }

  if (flags.args().size() == 2 && flags.args()[1] == "--version") {
    // for requester_env_.verify_command()
    VLOG(1) << trace_id << " --version";
    return IncludeProcessorResult::Ok(std::set<string>());
  }

  // LINK mode.
  if (flags.mode() == GCCFlags::LINK) {
    return RunLinkIncludeProcessor(trace_id, flags, compiler_info,
                                   command_spec);
  }

  // go into the usual path.
  return CxxCompilerTypeSpecific::RunIncludeProcessor(
      trace_id, compiler_flags, compiler_info, command_spec, file_stat_cache);
}

CompilerTypeSpecific::IncludeProcessorResult
GCCCompilerTypeSpecific::RunThinLTOImports(const string& trace_id,
                                           const GCCFlags& flags) {
  ThinLTOImportProcessor processor;
  std::set<string> required_files;
  if (!processor.GetIncludeFiles(flags.thinlto_index(), flags.cwd(),
                                 &required_files)) {
    LOG(ERROR) << trace_id << " failed to get ThinLTO imports";
    return IncludeProcessorResult::ErrorToLog("failed to get ThinLTO imports");
  }

  return IncludeProcessorResult::Ok(std::move(required_files));
}

CompilerTypeSpecific::IncludeProcessorResult
GCCCompilerTypeSpecific::RunLinkIncludeProcessor(
    const string& trace_id,
    const GCCFlags& flags,
    const CompilerInfo& compiler_info,
    const CommandSpec& command_spec) {
  LinkerInputProcessor linker_input_processor(flags.args(), flags.cwd());

  std::set<string> required_files;
  std::vector<string> system_library_paths;
  if (!linker_input_processor.GetInputFilesAndLibraryPath(
          compiler_info, command_spec, &required_files,
          &system_library_paths)) {
    return IncludeProcessorResult::ErrorToLog("failed to get input files " +
                                              flags.DebugString());
  }

  IncludeProcessorResult result =
      IncludeProcessorResult::Ok(std::move(required_files));
  result.system_library_paths = std::move(system_library_paths);
  return result;
}

}  // namespace devtools_goma
