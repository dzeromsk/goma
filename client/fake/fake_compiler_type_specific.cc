// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake/fake_compiler_type_specific.h"

#include "fake/fake_include_processor.h"
#include "fake_flags.h"
#include "glog/logging.h"

namespace devtools_goma {

std::unique_ptr<CompilerInfoData>
FakeCompilerTypeSpecific::BuildCompilerInfoData(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  return compiler_info_builder_.FillFromCompilerOutputs(
      flags, local_compiler_path, compiler_info_envs);
}

CompilerTypeSpecific::IncludeProcessorResult
FakeCompilerTypeSpecific::RunIncludeProcessor(
    const string& trace_id,
    const CompilerFlags& compiler_flags,
    const CompilerInfo& compiler_info,
    const CommandSpec& command_spec,
    FileStatCache* file_stat_cache) {
  DCHECK_EQ(CompilerFlagType::Fake, compiler_flags.type());

  FakeIncludeProcessor include_processor;
  std::set<string> required_files;
  if (!include_processor.Run(
          trace_id, static_cast<const FakeFlags&>(compiler_flags),
          ToFakeCompilerInfo(compiler_info), &required_files)) {
    return IncludeProcessorResult::ErrorToLog(
        "failed to run fake include processor");
  }

  return IncludeProcessorResult::Ok(std::move(required_files));
}

}  // namespace devtools_goma
