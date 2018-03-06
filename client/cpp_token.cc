// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_token.h"

namespace {

int Mul(int v1, int v2) { return v1 * v2; }
int Div(int v1, int v2) { return v2 == 0 ? 0 : v1 / v2; }
int Mod(int v1, int v2) { return v2 == 0 ? 0 : v1 % v2; }
int Add(int v1, int v2) { return v1 + v2; }
int Sub(int v1, int v2) { return v1 - v2; }
int RShift(int v1, int v2) { return v1 >> v2; }
int LShift(int v1, int v2) { return v1 << v2; }
int Gt(int v1, int v2) { return v1 > v2; }
int Lt(int v1, int v2) { return v1 < v2; }
int Ge(int v1, int v2) { return v1 >= v2; }
int Le(int v1, int v2) { return v1 <= v2; }
int Eq(int v1, int v2) { return v1 == v2; }
int Ne(int v1, int v2) { return v1 != v2; }
int And(int v1, int v2) { return v1 & v2; }
int Xor(int v1, int v2) { return v1 ^ v2; }
int Or(int v1, int v2) { return v1 | v2; }
int LAnd(int v1, int v2) { return v1 && v2; }
int LOr(int v1, int v2) { return v1 || v2; }

}  // anonymous namespace

namespace devtools_goma {

const int CppToken::kPrecedenceTable[] = {
  9, 9, 9,      // MUL, DIV, MOD,
  8, 8,         // ADD, SUB,
  7, 7,         // RSHIFT, LSHIFT,
  6, 6, 6, 6,   // GT, LT, GE, LE,
  5, 5,         // EQ, NE,
  4,            // AND,
  3,            // XOR,
  2,            // OR,
  1,            // LAND,
  0,            // LOR,
};

const CppToken::OperatorFunction CppToken::kFunctionTable[] = {
  Mul, Div, Mod, Add, Sub, RShift, LShift, Gt, Lt, Ge, Le, Eq, Ne,
  And, Xor, Or, LAnd, LOr
};

std::string CppToken::DebugString() const {
  std::string str;
  str.reserve(16);
  switch (type) {
    case IDENTIFIER:
      str.append("[IDENT(");
      str.append(string_value);
      str.append(")]");
      break;
    case STRING:
      str.append("[STRING(\"");
      str.append(string_value);
      str.append("\")]");
      break;
    case NUMBER:
      str.append("[NUMBER(");
      str.append(string_value);
      str.append(", ");
      str.append(std::to_string(v.int_value));
      str.append(")]");
      break;
    case DOUBLESHARP:
      return "[##]";
    case TRIPLEDOT:
      return "[...]";
    case NEWLINE:
      return "[NL]\n";
    case ESCAPED:
      str.append("[\\");
      str.push_back(v.char_value.c);
      str.append("]");
      break;
    case MACRO_PARAM:
      str.append("[MACRO_PARAM(arg");
      str.append(std::to_string(v.param_index));
      str.append(")]");
      break;
    case MACRO_PARAM_VA_ARGS:
      str.append("[MACRO_PARAM_VA_ARGS]");
      break;
    case CHAR_LITERAL:
      str.append("[CHAR_LITERAL(");
      str.append(std::to_string(v.int_value));
      str.append(")]");
      break;
    case END:
      return "[END]";
    case BEGIN_HIDE:
      str.append("[BEGIN_HIDE(" + std::to_string(v.int_value) + ")]");
      break;
    case END_HIDE:
      str.append("[END_HIDE(" + std::to_string(v.int_value) + ")]");
      break;
    default:
      str.append("[");
      if (!string_value.empty()) {
        str.append(string_value);
      } else if (v.char_value.c) {
        str.push_back(v.char_value.c);
      } else {
        str.append(v.char_value.c2);
      }
      str.append("]");
  }
  return str;
}

std::string CppToken::GetCanonicalString() const {
  if (!string_value.empty())
    return string_value;
  if (v.char_value.c)
    return std::string() + v.char_value.c;
  return v.char_value.c2;
}

}  // namespace devtools_goma
