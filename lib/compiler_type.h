// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_COMPILER_TYPE_H_
#define DEVTOOLS_GOMA_LIB_COMPILER_TYPE_H_

#include <ostream>

namespace devtools_goma {

enum class CompilerType {
  Unknown,    // unknown type
  Gcc,        // gcc or clang
  Clexe,      // cl.exe or clang-cl.exe
  ClangTidy,  // clang_tidy
  Javac,      // javac
  Java,       // java
};

// Add operator<< for glog and gtest.
std::ostream& operator<<(std::ostream& os, CompilerType type);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_TYPE_H_
