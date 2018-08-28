// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_COMPILER_FLAG_TYPE_H_
#define DEVTOOLS_GOMA_LIB_COMPILER_FLAG_TYPE_H_

#include <ostream>

namespace devtools_goma {

enum class CompilerFlagType {
  Unknown,    // unknown type
  Fake,       // fake compiler
  Gcc,        // gcc or clang
  Clexe,      // cl.exe or clang-cl.exe
  ClangTidy,  // clang_tidy
  Javac,      // javac
  Java,       // java
};

// Add operator<< for glog and gtest.
std::ostream& operator<<(std::ostream& os, CompilerFlagType type);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_FLAG_TYPE_H_
