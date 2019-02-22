// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_VC_COMPILER_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_VC_COMPILER_TYPE_SPECIFIC_H_

#include "cxx/cxx_compiler_type_specific.h"
#include "cxx/vc_compiler_info_builder.h"

namespace devtools_goma {

class VCCompilerTypeSpecific : public CxxCompilerTypeSpecific {
 public:
  ~VCCompilerTypeSpecific() override = default;

  VCCompilerTypeSpecific(const VCCompilerTypeSpecific&) = delete;
  void operator=(const VCCompilerTypeSpecific&) = delete;

  bool RemoteCompileSupported(const string& trace_id,
                              const CompilerFlags& flags,
                              bool verify_output) const override;

  std::unique_ptr<CompilerInfoData> BuildCompilerInfoData(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs) override;

  bool SupportsDepsCache(const CompilerFlags&) const override { return true; }

 private:
  VCCompilerTypeSpecific() = default;

  VCCompilerInfoBuilder compiler_info_builder_;

  friend class CompilerTypeSpecificCollection;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_VC_COMPILER_TYPE_SPECIFIC_H_
