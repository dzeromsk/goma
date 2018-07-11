// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_BUILDER_FACADE_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_BUILDER_FACADE_H_

#include <map>
#include <ostream>
#include <string>

#include "clang_tidy/clang_tidy_compiler_info_builder.h"
#include "compiler_flags.h"
#include "compiler_info_builder.h"
#include "compiler_specific.h"
#include "cxx/gcc_compiler_info_builder.h"
#include "cxx/vc_compiler_info_builder.h"
#include "java/java_compiler_info_builder.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

// CompilerInfoBuilderFacade provides methods to construct CompilerInfoData.
//
// How to use:
// CompielrInfoBuilderFacade cib;
// std::unique_ptr<CompilerInfoData> data(
//     cib.FillFromCompilerOutputs(....));
// CompilerInfo compiler_info(std::move(data));
class CompilerInfoBuilderFacade {
 public:
  // Creates new CompilerInfoData* from compiler outputs.
  // if found is true and error_message in it is empty,
  // it successfully gets compiler info.
  // if found is true and error_message in it is not empty,
  // it finds local compiler but failed to get some information, such as
  // system include paths.
  // if found is false if it fails to find local compiler.
  // Caller should take ownership of returned CompilerInfoData.
  std::unique_ptr<CompilerInfoData> FillFromCompilerOutputs(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs);

  void SetHashRewriteRule(const std::map<string, string>& rule);
  void Dump(std::ostringstream* ss);

 private:
  GCCCompilerInfoBuilder gcc_builder_;
  VCCompilerInfoBuilder vc_builder_;
  ClangTidyCompilerInfoBuilder clang_tidy_builder_;
  JavacCompilerInfoBuilder javac_builder_;
  JavaCompilerInfoBuilder java_builder_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_BUILDER_FACADE_H_
