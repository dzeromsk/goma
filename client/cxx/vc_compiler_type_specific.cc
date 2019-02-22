// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vc_compiler_type_specific.h"
#include "glog/logging.h"

namespace devtools_goma {

bool VCCompilerTypeSpecific::RemoteCompileSupported(const string& trace_id,
                                                    const CompilerFlags& flags,
                                                    bool verify_output) const {
  const VCFlags& vc_flag = static_cast<const VCFlags&>(flags);
  // GOMA doesn't work with PCH so we generate it only for local builds.
  if (!vc_flag.creating_pch().empty()) {
    LOG(INFO) << trace_id
              << " force fallback. cannot create pch in goma backend.";
    return false;
  }
  if (vc_flag.require_mspdbserv()) {
    LOG(INFO) << trace_id
              << " force fallback. cannot run mspdbserv in goma backend.";
    return false;
  }

  return true;
}

std::unique_ptr<CompilerInfoData> VCCompilerTypeSpecific::BuildCompilerInfoData(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  return compiler_info_builder_.FillFromCompilerOutputs(
      flags, local_compiler_path, compiler_info_envs);
}

}  // namespace devtools_goma
