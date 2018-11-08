// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_PARSER_H_

#include "token.h"
#include "type.h"

namespace devtools_goma {
namespace modulemap {

// Parser is a modulemap parser.
//
// Basic usage is just to run Parser::Run() (after Lexer::Run).
class Parser {
 public:
  // Parse modulemap file. Returns true if succeeded, false otherwise.
  // The parsed result will be set to |module_map|.
  static bool Run(const std::vector<Token>& tokens, ModuleMap* module_map);

 private:
  explicit Parser(const std::vector<Token>& tokens)
      : tokens_(tokens), end_(Token::End()) {}

  // Returns the current token.
  // If everything is consumed, END token is returned.
  const Token& current() const {
    return pos_ < tokens_.size() ? tokens_[pos_] : end_;
  }
  // Returns the next token. If not exist, END token is returned.
  const Token& next() const {
    return pos_ + 1 < tokens_.size() ? tokens_[pos_ + 1] : end_;
  }

  // If the current token type is ident, returns true and stores the value
  // in |ident|.
  // Otherwise, returns false and doesn't consume anything.
  bool ParseIdent(string* ident);
  // If the current token type is string, returns true and stores the value
  // in |s|.
  // Otherwise, returns false and doesn't consume anything.
  bool ParseString(string* s);
  // If the current token type is integer, returns true and stores the value
  // in |s|.
  // Otherwise, returns false and doesn't consume anything.
  bool ParseInteger(string* s);

  // If the current token is punc and value is |c|, consumes it and
  // returns true. Otherwise, returns false and doesn't consume anything.
  bool ConsumePunc(char c);
  // If the current token is ident and value is |ident|, consumes it and
  // returns true. Otherwise, returns false and doesn't consume anything.
  bool ConsumeIdent(absl::string_view ident);

  // The following functiosn are to parse syntax.
  // ParseXXX is to parse syntax XXX. If succeeded, ParseXXX consumes necessary
  // tokens and returns true.
  // If parse failed, false is returned. The parser internal state
  // is undefined in this case. You should consider the modulemap file is
  // broken.
  //
  // ParseXXXOpt means, it consumes nothing (and returns true) or
  // it's the same as ParseXXX.
  bool ParseModuleMapFile(ModuleMap* module_map);
  bool ParseModuleId(string* module_id);
  bool ParseModuleDeclaration(Module* module_decl);
  bool ParseModuleMembersOpt(Module* module_decl);
  bool ParseRequiresDeclaration(std::vector<Feature>* features);
  bool ParseFeatureList(std::vector<Feature>* features);
  bool ParseFeature(Feature* feature);
  bool ParseHeaderDeclaration(Header* header);
  bool ParseHeaderAttrs(Header* header);
  bool ParseHeaderAttrsOpt(Header* header);
  bool ParseHeaderAttr(Header* header);
  bool ParseUmbrellaDirDeclaration(string* dirname);
  bool ParseSubmoduleDeclaration(Module* module_decl);
  bool ParseInferredSubmoduleMember(Module* module_decl);
  bool ParseExportDeclaration(string* module_id);
  bool ParseWildcardModuleId(string* module_id);
  bool ParseExportAsDeclaration(string* export_as);
  bool ParseUseDeclaration(string* module_id);
  bool ParseLinkDeclaration(Link* link);
  bool ParseConfigMacrosDeclaration(ConfigMacro* config_macro);
  bool ParseConfigMacroList(std::vector<string>* names);
  bool ParseConflictDeclaration(Conflict* conflict);
  bool ParseAttributesOpt(std::vector<string>*);
  bool ParseAttributes(std::vector<string>*);
  bool ParseAttribute(string*);

  // internal state.
  const std::vector<Token>& tokens_;
  size_t pos_ = 0;

  // Keep |end_| as an instance to return a reference in Next().
  // I don't make this `static` since Token is not trivially destructible.
  // performance penalty must be ignoreable.
  const Token end_;
};

}  // namespace modulemap
}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_PARSER_H_
