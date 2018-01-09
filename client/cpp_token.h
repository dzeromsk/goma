// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CPP_TOKEN_H_
#define DEVTOOLS_GOMA_CLIENT_CPP_TOKEN_H_

#include <string>
#include <type_traits>

#include "glog/logging.h"

#include "string_piece.h"

namespace devtools_goma {

struct CppToken {
  enum Type {
    IDENTIFIER, STRING, NUMBER, SHARP, DOUBLESHARP, TRIPLEDOT,
    SPACE, NEWLINE, ESCAPED, PUNCTUATOR, END, MACRO_PARAM,
    MACRO_PARAM_VA_ARGS,

    // Operators
    OP_BEGIN,
    MUL = OP_BEGIN, DIV, MOD, ADD, SUB, RSHIFT, LSHIFT, GT, LT,
    GE, LE, EQ, NE, AND, XOR, OR, LAND, LOR,

    // Hideset annotation.
    // This is used only for macro expansion.
    // If token has below type, it should have macro id in v.int_value.
    BEGIN_HIDE, END_HIDE
  };

  typedef int (*OperatorFunction)(int, int);

  CppToken() : type(END) {}
  explicit CppToken(Type type) : type(type) {}
  explicit CppToken(int i) : type(NUMBER), v(i) {}
  CppToken(Type type, char c) : type(type), v(c) {}
  CppToken(Type type, char c1, char c2) : type(type), v(c1, c2) {}
  CppToken(Type type, int i) : type(type) {
    v.int_value = i;
  }
  CppToken(Type type, StringPiece s) : type(type), string_value(s) {}

  bool operator==(const CppToken& other) const {
    if (type != other.type) {
      return false;
    }
    if (type == NUMBER) {
      return v.int_value == other.v.int_value;
    }

    return DebugString() == other.DebugString();
  }

  void Append(const char* str, size_t size);
  void Append(const std::string& str);
  bool IsPuncChar(int c) const;
  bool IsOperator() const;
  void MakeMacroParam(size_t param_index);
  void MakeMacroParamVaArgs();

  std::string DebugString() const;
  std::string GetCanonicalString() const;

  int ApplyOperator(int v1, int v2) const {
    DCHECK(IsOperator());
    return kFunctionTable[type - OP_BEGIN](v1, v2);
  }
  OperatorFunction GetOperator() const {
    DCHECK(IsOperator());
    return kFunctionTable[type - OP_BEGIN];
  }
  int GetPrecedence() const {
    DCHECK(IsOperator());
    return kPrecedenceTable[type - OP_BEGIN];
  }

  static const OperatorFunction kFunctionTable[];
  static const int kPrecedenceTable[];

  Type type;
  std::string string_value;

  // A struct to hold char value(s) for operators and punctuators.
  struct CharValue {
    // For one-char tokens.
    char c;
    // For two-char tokens; c is always set to zero when c2 has a value.
    char c2[3];
  };

  union value {
    value() : param_index(0) {}
    value(int i) : param_index(i) {}
    value(char c) : param_index(0) {
      char_value.c = c;
    }
    value(char c1, char c2) {
      char_value.c = 0;
      char_value.c2[0] = c1;
      char_value.c2[1] = c2;
      char_value.c2[2] = 0;
    }
    CharValue char_value;
    long int_value;
    size_t param_index;
  } v;
};

inline void CppToken::Append(const char* str, size_t size) {
  string_value.append(str, size);
}

inline void CppToken::Append(const std::string& str) {
  string_value.append(str);
}

inline bool CppToken::IsPuncChar(int c) const {
  return ((type == PUNCTUATOR || type >= OP_BEGIN) && v.int_value == c);
}

inline bool CppToken::IsOperator() const {
  return (type >= OP_BEGIN);
}

inline void CppToken::MakeMacroParam(size_t param_index) {
  DCHECK_EQ(IDENTIFIER, type);
  type = MACRO_PARAM;
  v.param_index = param_index;
  string_value.clear();
}

inline void CppToken::MakeMacroParamVaArgs() {
  DCHECK_EQ(IDENTIFIER, type);
  DCHECK_EQ("__VA_ARGS__", string_value);
  type = MACRO_PARAM_VA_ARGS;
  string_value.clear();
}

static_assert(std::is_nothrow_move_constructible<CppToken>::value,
              "CppToken must be move constructible");

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CPP_TOKEN_H_
