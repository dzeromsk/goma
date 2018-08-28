// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cxx/cxx_compiler_type_specific.h"

#include "cxx/cxx_compiler_info.h"
#include "cxx/include_processor/cpp_include_processor.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"

namespace devtools_goma {

CompilerTypeSpecific::IncludeProcessorResult
CxxCompilerTypeSpecific::RunIncludeProcessor(
    const string& trace_id,
    const CompilerFlags& compiler_flags,
    const CompilerInfo& compiler_info,
    const CommandSpec& command_spec,
    FileStatCache* file_stat_cache) {
  const CxxCompilerInfo& info = ToCxxCompilerInfo(compiler_info);

  // We don't support multiple input files.
  if (compiler_flags.input_filenames().size() != 1U) {
    LOG(ERROR) << trace_id << " multiple inputs? "
               << compiler_flags.input_filenames().size() << " "
               << compiler_flags.input_filenames();
    return IncludeProcessorResult::ErrorToUser(
        "multiple inputs are not supported.");
  }

  const string& input_filename = compiler_flags.input_filenames()[0];

  CppIncludeProcessor include_processor;
  std::set<string> required_files;
  bool ok = include_processor.GetIncludeFiles(
      input_filename, compiler_flags.cwd_for_include_processor(),
      compiler_flags, info, &required_files, file_stat_cache);

  if (!ok) {
    return IncludeProcessorResult::ErrorToLog(
        "failed to run cpp include processor");
  }

  IncludeProcessorResult result =
      IncludeProcessorResult::Ok(std::move(required_files));
  result.total_files = include_processor.total_files();
  result.skipped_files = include_processor.skipped_files();
  return result;
}

}  // namespace devtools_goma
