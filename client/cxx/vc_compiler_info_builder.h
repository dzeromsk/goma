// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_VC_COMPILER_INFO_BUILDER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_VC_COMPILER_INFO_BUILDER_H_

#include <string>
#include <vector>

#include "compiler_specific.h"
#include "cxx_compiler_info_builder.h"
#include "vc_flags.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

class VCCompilerInfoBuilder : public CxxCompilerInfoBuilder {
 public:
  ~VCCompilerInfoBuilder() override = default;

  void SetTypeSpecificCompilerInfo(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const override;

  // Parses output of "cl.exe", and extracts |version| and |target|.
  static bool ParseVCVersion(const string& vc_logo,
                             string* version,
                             string* target);

  // Execute VC and get the string output for VC version
  static bool GetVCVersion(const string& cl_exe_path,
                           const std::vector<string>& env,
                           const string& cwd,
                           string* version,
                           string* target);

  // Parses output of "cl.exe /nologo /Bxvcflags.exe non-exist-file.cpp" (C++)
  // or "cl.exe /nologo /B1vcflags.exe non-exist-file.c" (C),
  // and extracts |include_paths| and |predefined macros| in
  // "#define FOO X\n" format.
  // |predefined_macros| may be NULL (don't capture predefined macros
  // in this case).
  static bool ParseVCOutputString(const string& output,
                                  std::vector<string>* include_paths,
                                  string* predefined_macros);

  static bool GetVCDefaultValues(const string& cl_exe_path,
                                 const string& vcflags_path,
                                 const std::vector<string>& compiler_info_flags,
                                 const std::vector<string>& compiler_info_envs,
                                 const string& cwd,
                                 const string& lang,
                                 CompilerInfoData* compiler_info);

 private:
  // SetTypeSpecificCompilerInfo for cl.exe
  void SetClexeSpecificCompilerInfo(
      const VCFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const;
  // SetTypeSpecificCompilerInfo for clang-cl.exe
  void SetClangClSpecificCompilerInfo(
      const VCFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_VC_COMPILER_INFO_BUILDER_H_
