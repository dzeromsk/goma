// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_TYPE_SPECIFIC_COLLECTION_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_TYPE_SPECIFIC_COLLECTION_H_

#include "clang_tidy/clang_tidy_compiler_type_specific.h"
#include "compiler_flag_type.h"
#include "compiler_type_specific.h"
#include "cxx/gcc_compiler_type_specific.h"
#include "cxx/vc_compiler_type_specific.h"
#include "fake/fake_compiler_type_specific.h"
#include "java/java_compiler_type_specific.h"

namespace devtools_goma {

// CompilerTypeSpecificCollection contains all CompilerTypeSpecific.
// TODO: Instead of having all at once, register?
class CompilerTypeSpecificCollection {
 public:
  // Takes CompilerTypeSpecific from CompilerFlagType.
  CompilerTypeSpecific* Get(CompilerFlagType type);

 private:
  // TODO: Consider using map (FlagType -> CompilerTypeSpecific)
  // instead of listing all compiler type specific here?
  GCCCompilerTypeSpecific gcc_;
  VCCompilerTypeSpecific vc_;
  ClangTidyCompilerTypeSpecific clang_tidy_;
  JavacCompilerTypeSpecific javac_;
  JavaCompilerTypeSpecific java_;
  FakeCompilerTypeSpecific fake_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_TYPE_SPECIFIC_COLLECTION_H_
