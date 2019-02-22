// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_TYPE_SPECIFIC_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/types/optional.h"
#include "compiler_flags.h"
#include "compiler_info.h"
#include "compiler_specific.h"
#include "file_stat_cache.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

// CompilerTypeSpecific contains compiler type specific routines.
class CompilerTypeSpecific {
 public:
  struct IncludeProcessorResult;

  virtual ~CompilerTypeSpecific() = default;

  // Returns true if remote compile is supported.
  virtual bool RemoteCompileSupported(const string& trace_id,
                                      const CompilerFlags& flags,
                                      bool verify_output) const = 0;

  // Builds CompilerInfoData.
  virtual std::unique_ptr<CompilerInfoData> BuildCompilerInfoData(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs) = 0;

  // Returns true if DepsCache is supported.
  virtual bool SupportsDepsCache(const CompilerFlags& flags) const = 0;

  // Runs include processor.
  // |trace_id| is used only for logging purpose.
  // The required_files of the return result should be in relative path form
  // from cwd (of compiler_flags).
  virtual IncludeProcessorResult RunIncludeProcessor(
      const string& trace_id,
      const CompilerFlags& compiler_flags,
      const CompilerInfo& compiler_info,
      const CommandSpec& command_spec,
      FileStatCache* file_stat_cache) = 0;
};

struct CompilerTypeSpecific::IncludeProcessorResult {
  // Ok means IncludeProcessor run correctly.
  static IncludeProcessorResult Ok(std::set<string> required_files);

  // ErrorToLog means IncludeProcessor didn't finish. However, it's an internal
  // error, so compile task should be fallen back. Error is logged, but won't be
  // shown to a user.
  static IncludeProcessorResult ErrorToLog(string error_reason);

  // ErrorToUser means IncludeProcessor didn't finish due to user's input.
  // Error is delivered to a user.
  static IncludeProcessorResult ErrorToUser(string error_reason);

  // true if IncludeProcessor run correctly.
  bool ok = false;
  // the set of include files.
  std::set<string> required_files;
  bool error_to_user = false;
  string error_reason;

  // optional. used in linker include processor.
  std::vector<string> system_library_paths;

  // optional. stats if any.
  absl::optional<int> total_files;
  absl::optional<int> skipped_files;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_TYPE_SPECIFIC_H_
