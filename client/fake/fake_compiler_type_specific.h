// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_COMPILER_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_COMPILER_TYPE_SPECIFIC_H_

#include "compiler_type_specific.h"
#include "fake/fake_compiler_info_builder.h"

namespace devtools_goma {

class FakeCompilerTypeSpecific : public CompilerTypeSpecific {
 public:
  FakeCompilerTypeSpecific(const FakeCompilerTypeSpecific&) = delete;
  void operator=(const FakeCompilerTypeSpecific&) = delete;

  std::unique_ptr<CompilerInfoData> BuildCompilerInfoData(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs) override;

  bool SupportsDepsCache(const CompilerFlags&) const override { return false; }

  IncludeProcessorResult RunIncludeProcessor(
      const string& trace_id,
      const CompilerFlags& compiler_flags,
      const CompilerInfo& compiler_info,
      const CommandSpec& command_spec,
      FileStatCache* file_stat_cache) override;

 private:
  FakeCompilerTypeSpecific() = default;

  FakeCompilerInfoBuilder compiler_info_builder_;

  friend class CompilerTypeSpecificCollection;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_COMPILER_TYPE_SPECIFIC_H_
