// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_directive.h"

#include <sstream>

#include "basictypes.h"

namespace devtools_goma {

const char* CppDirectiveTypeToString(CppDirectiveType type) {
  switch (type) {
  case CppDirectiveType::DIRECTIVE_INCLUDE:      return "include";
  case CppDirectiveType::DIRECTIVE_IMPORT:       return "import";
  case CppDirectiveType::DIRECTIVE_INCLUDE_NEXT: return "include_next";
  case CppDirectiveType::DIRECTIVE_DEFINE:       return "define";
  case CppDirectiveType::DIRECTIVE_UNDEF:        return "undef";
  case CppDirectiveType::DIRECTIVE_IFDEF:        return "ifdef";
  case CppDirectiveType::DIRECTIVE_IFNDEF:       return "ifndef";
  case CppDirectiveType::DIRECTIVE_IF:           return "if";
  case CppDirectiveType::DIRECTIVE_ELSE:         return "else";
  case CppDirectiveType::DIRECTIVE_ENDIF:        return "endif";
  case CppDirectiveType::DIRECTIVE_ELIF:         return "elif";
  case CppDirectiveType::DIRECTIVE_PRAGMA:       return "pragma";
  case CppDirectiveType::DIRECTIVE_ERROR:
    // Since DIRECTIVE_ERROR is not #error, <error> is used here.
    return "<error>";
  }

  return "<unexpected>";
}

// static
std::unique_ptr<CppDirective> CppDirective::Error(string reason) {
  return std::unique_ptr<CppDirective>(
      new CppDirectiveError(std::move(reason)));
}

// static
std::unique_ptr<CppDirective> CppDirective::Error(string reason, string arg) {
  return std::unique_ptr<CppDirective>(
      new CppDirectiveError(std::move(reason), std::move(arg)));
}

string CppDirectiveIncludeBase::DebugString() const {
  std::ostringstream os;
  os << '#' << DirectiveTypeName();
  switch (delimiter()) {
  case '<':
    os << '<' << filename() << '>';
    break;
  case '"':
    os << '"' << filename() << '"';
    break;
  default:
    for (const auto& t : tokens()) {
      os << t.DebugString();
    }
  }

  return os.str();
}

string CppDirectiveDefine::DebugString() const {
  std::ostringstream os;
  os << "#define " << name();
  if (is_function_macro()) {
    os << '(';
    bool first = true;
    for (int i = 0; i < num_args(); ++i) {
      if (first) {
        first = false;
      } else {
        os << ", ";
      }
      os << '_' << (i + 1);
    }
    if (has_vararg()) {
      if (!first) {
        os << ", ";
      }
      os << "__VA_ARGS__";
    }
    os << ") ";
  } else {
    os << " ";
  }

  for (const auto& t : replacement()) {
    os << t.DebugString();
  }

  return os.str();
}

string CppDirectiveIf::DebugString() const {
  std::ostringstream os;
  os << "#if ";
  for (const auto& t : tokens()) {
    os << t.DebugString();
  }
  return os.str();
}

string CppDirectiveElif::DebugString() const {
  std::ostringstream os;
  os << "#elif ";
  for (const auto& t : tokens()) {
    os << t.DebugString();
  }
  return os.str();
}

}  // namespace devtools_goma
