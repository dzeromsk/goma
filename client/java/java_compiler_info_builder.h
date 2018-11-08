// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_INFO_BUILDER_H_
#define DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_INFO_BUILDER_H_

#include <string>
#include <vector>

#include "compiler_info_builder.h"

using std::string;

namespace devtools_goma {

class JavacCompilerInfoBuilder : public CompilerInfoBuilder {
 public:
  ~JavacCompilerInfoBuilder() override = default;

  void SetLanguageExtension(CompilerInfoData* data) const override;

  void SetTypeSpecificCompilerInfo(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const override;

  // Parses output of "javac", and extracts |version|.
  static bool ParseJavacVersion(const string& vc_logo, string* version);

  // Execute javac and get the string output for javac version
  static bool GetJavacVersion(const string& javac,
                              const std::vector<string>& compiler_info_envs,
                              const string& cwd,
                              string* version);
};

class JavaCompilerInfoBuilder : public CompilerInfoBuilder {
 public:
  ~JavaCompilerInfoBuilder() override = default;

  void SetLanguageExtension(CompilerInfoData* data) const override;

  void SetTypeSpecificCompilerInfo(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const override;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_INFO_BUILDER_H_
