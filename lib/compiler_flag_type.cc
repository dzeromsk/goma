// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/compiler_flag_type.h"

namespace devtools_goma {

std::ostream& operator<<(std::ostream& os, CompilerFlagType type) {
  switch (type) {
    case CompilerFlagType::Unknown:
      return os << "unknown";
    case CompilerFlagType::Fake:
      return os << "fake";
    case CompilerFlagType::Gcc:
      return os << "gcc";
    case CompilerFlagType::Clexe:
      return os << "clexe";
    case CompilerFlagType::ClangTidy:
      return os << "clang_tidy";
    case CompilerFlagType::Javac:
      return os << "javac";
    case CompilerFlagType::Java:
      return os << "java";
  }
}

}  // namespace devtools_goma
