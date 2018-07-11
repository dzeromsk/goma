// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CLANG_TIDY_CLANG_TIDY_COMPILER_INFO_BUILDER_H_
#define DEVTOOLS_GOMA_CLIENT_CLANG_TIDY_CLANG_TIDY_COMPILER_INFO_BUILDER_H_

#include <string>
#include <vector>

#include "cxx/cxx_compiler_info_builder.h"

using std::string;

namespace devtools_goma {

class ClangTidyCompilerInfoBuilder : public CxxCompilerInfoBuilder {
 public:
  ~ClangTidyCompilerInfoBuilder() override = default;

  void SetTypeSpecificCompilerInfo(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const override;

  // Executes clang-tidy and gets the string output for clang-tidy version.
  static bool GetClangTidyVersionTarget(
      const string& clang_tidy_path,
      const std::vector<string>& compiler_info_envs,
      const string& cwd,
      string* version,
      string* target);
  static bool ParseClangTidyVersionTarget(const string& output,
                                          string* version,
                                          string* target);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CLANG_TIDY_CLANG_TIDY_COMPILER_INFO_BUILDER_H_
