// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CPP_MACRO_H_
#define DEVTOOLS_GOMA_CLIENT_CPP_MACRO_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "cpp_token.h"
#include "file_id.h"

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
    // UNDEFINED represents macro is referenced without define,
    // or deleted by #undef
    UNDEFINED,
    OBJ,
    FUNC,
    CBK,
    CBK_FUNC,
    // UNUSED represents macro in macro cache is not referenced and defined in
    // current preprocessing.
    UNUSED,
  };
  explicit Macro(int id)
      : id(id), type(UNDEFINED), callback(NULL),
        callback_func(NULL), num_args(0), is_vararg(false),
        has_identifier_in_replacement(false), macro_pos(0) {}
  Macro(int id, Type type)
      : id(id), type(type),
        callback(NULL), callback_func(NULL), num_args(0), is_vararg(false),
        has_identifier_in_replacement(false), macro_pos(0) {}

  bool IsMatch(const FileId& fid, size_t pos) const {
    return fileid.IsValid() && fid == fileid && pos == macro_pos;
  }
  string DebugString(CppParser* parser, const string& name) const;

  int id;
  Type type;
  ArrayTokenList replacement;
  CallbackObj callback;
  CallbackFunc callback_func;
  size_t num_args;
  bool is_vararg;
  bool has_identifier_in_replacement;

  // fileid and macro_pos represent position and fileid of file
  // that macro is defined. This is used to check validness of cached macro.
  FileId fileid;
  size_t macro_pos;
};


// MacroEnv is a map from macro name to macro. It includes parsed macro set.
// CppParser will take one instance of MacroEnv.
// At first, type of each macro is UNUSED, but it's updated while parsing.
// Before returning this to macro env pool,
// every macro type is marked as UNUSED.
using MacroEnv = std::unordered_map<string, Macro>;

void InitMacroEnvCache();
void QuitMacroEnvCache();

std::unique_ptr<MacroEnv> GetMacroEnvFromCache();
void ReleaseMacroEnvToCache(std::unique_ptr<MacroEnv> macro);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CPP_MACRO_H_
