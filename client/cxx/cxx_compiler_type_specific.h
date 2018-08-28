// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_CXX_COMPILER_TYPE_SPECIFIC_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_CXX_COMPILER_TYPE_SPECIFIC_H_

#include "compiler_type_specific.h"

namespace devtools_goma {

class CxxCompilerTypeSpecific : public CompilerTypeSpecific {
 public:
  CxxCompilerTypeSpecific(const CxxCompilerTypeSpecific&) = delete;
  void operator=(const CxxCompilerTypeSpecific&) = delete;

  bool SupportsDepsCache() const override { return true; }

  IncludeProcessorResult RunIncludeProcessor(
      const string& trace_id,
      const CompilerFlags& compiler_flags,
      const CompilerInfo& compiler_info,
      const CommandSpec& command_spec,
      FileStatCache* file_stat_cache) override;

 protected:
  CxxCompilerTypeSpecific() = default;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_CXX_COMPILER_TYPE_SPECIFIC_H_
