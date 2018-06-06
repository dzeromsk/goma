// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_type.h"

namespace devtools_goma {

std::ostream& operator<<(std::ostream& os, CompilerType type) {
  switch (type) {
    case CompilerType::Unknown:
      return os << "unknown";
    case CompilerType::Gcc:
      return os << "gcc";
    case CompilerType::Clexe:
      return os << "clexe";
    case CompilerType::ClangTidy:
      return os << "clang_tidy";
    case CompilerType::Javac:
      return os << "javac";
    case CompilerType::Java:
      return os << "java";
  }
}

}  // namespace devtools_goma
