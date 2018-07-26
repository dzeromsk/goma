// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cxx/cxx_compiler_type_specific.h"

#include "glog/logging.h"

namespace devtools_goma {

CompilerTypeSpecific::IncludeProcessorResult
CxxCompilerTypeSpecific::RunIncludeProcessor(
    const string& trace_id,
    const CompilerFlags& compiler_flags,
    const CompilerInfo& compiler_info,
    const CommandSpec& command_spec,
    FileStatCache* file_stat_cache) {
  LOG(DFATAL) << "Not implemented yet. Will be migrated from CompileTask.";
  return IncludeProcessorResult ::ErrorToLog(
      "Not implemented yet. Will be migrated from CompileTask.");
}

}  // namespace devtools_goma
