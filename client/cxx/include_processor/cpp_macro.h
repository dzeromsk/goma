// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "cpp_token.h"
#include "glog/logging.h"

using std::string;

namespace devtools_goma {

class CppParser;

// There are two types of macros:
// 1. Object-like macro (OBJ):
//  # define identifier [space] replacement-list [NL]
//
// 2. Function-like macro (FUNC):
//  # define identifier '(' [identifier-list] ')' replacement-list [NL]
//  # define identifier '(' ... ')' replacement-list [NL]
//  # define identifier '(' identifier-list, ... ')' replacement-list [NL]
//
// CALLBACK and CALLBACK_FUNC types are internal macro types that are used
// for predefined macros (obj-like and func-like macros) that need to be
// evaluated at macro expansion time.
struct Macro {
  using Token = CppToken;
  using ArrayTokenList = std::vector<Token>;
  typedef Token (CppParser::*CallbackObj)();
  typedef Token (CppParser::*CallbackFunc)(const ArrayTokenList&);
  enum Type {
    OBJ,
    FUNC,
    CBK,
    CBK_FUNC,
  };

  // OBJ or FUNC
  Macro(string name,
        Type type,
        ArrayTokenList replacement,
        size_t num_args,
        bool is_vararg)
      : name(std::move(name)),
        type(type),
        replacement(std::move(replacement)),
        callback(nullptr),
        callback_func(nullptr),
        num_args(num_args),
        is_vararg(is_vararg),
        is_hidden(false),
        is_paren_balanced(IsParenBalanced(this->replacement)) {
    DCHECK(type == OBJ || type == FUNC) << type;
  }

  // CBK
  Macro(string name, Type type, CallbackObj obj)
      : name(std::move(name)),
        type(type),
        callback(obj),
        callback_func(nullptr),
        num_args(0),
        is_vararg(false),
        is_hidden(false),
        is_paren_balanced(true) {
    DCHECK_EQ(type, CBK);
  }

  // CBK_FUNC
  Macro(string name, Type type, CallbackFunc func, bool is_hidden)
      : name(std::move(name)),
        type(type),
        callback(nullptr),
        callback_func(func),
        num_args(1),  // CallbackFunc takes always 1 argument.
        is_vararg(false),
        is_hidden(is_hidden),
        is_paren_balanced(true) {
    DCHECK_EQ(type, CBK_FUNC);
  }

  static bool IsParenBalanced(const ArrayTokenList& tokens);

  string DebugString(CppParser* parser) const;
  bool IsPredefinedMacro() const { return type == CBK || type == CBK_FUNC; }

  const string name;
  const Type type;
  const ArrayTokenList replacement;
  const CallbackObj callback;
  const CallbackFunc callback_func;
  const size_t num_args;
  const bool is_vararg;
  // We say a macro is "hidden" when it is not "defined" but
  // callable. e.g. On GCC 5, defined(__has_include__) is 0
  // but __has_include__ can be used.
  const bool is_hidden;
  const bool is_paren_balanced;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_H_
