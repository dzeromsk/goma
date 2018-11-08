// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// The definition of clang module can be parsed with LL parser.
// The original grammar is LL(4), but almost LL(1).
//
// In the comment, first(X) means possible first tokens of definition X.
// If you're not familar with LL(k) grammar (you should be),
// see https://en.wikipedia.org/wiki/LL_parser

#include "parser.h"

namespace {

// preserved keywords
const char kConfigMacros[] = "config_macros";
const char kConflict[] = "conflict";
const char kExclude[] = "exclude";
const char kExplicit[] = "explicit";
const char kExtern[] = "extern";
const char kExport[] = "export";
const char kExportAs[] = "export_as";
const char kFramework[] = "framework";
const char kHeader[] = "header";
const char kLink[] = "link";
const char kModule[] = "module";
const char kPrivate[] = "private";
const char kRequires[] = "requires";
const char kTextual[] = "textual";
const char kUmbrella[] = "umbrella";
const char kUse[] = "use";

// used in header attrs
const char kSize[] = "size";
const char kMtime[] = "mtime";

}  // anonymous namespace

namespace devtools_goma {
namespace modulemap {

// static
bool Parser::Run(const std::vector<Token>& tokens, ModuleMap* module_map) {
  Parser parser(tokens);
  if (!parser.ParseModuleMapFile(module_map)) {
    return false;
  }

  // Check there is no more tokens.
  return parser.current().type() == Token::Type::END;
}

bool Parser::ParseIdent(string* ident) {
  if (current().type() == Token::Type::IDENT) {
    *ident = current().value();
    pos_ += 1;
    return true;
  }

  return false;
}

bool Parser::ParseString(string* s) {
  if (current().type() == Token::Type::STRING) {
    *s = current().value();
    pos_ += 1;
    return true;
  }

  return false;
}

bool Parser::ParseInteger(string* s) {
  if (current().type() == Token::Type::INTEGER) {
    *s = current().value();
    pos_ += 1;
    return true;
  }

  return false;
}

// If current is punctuation (value = c), consume it and returns true.
bool Parser::ConsumePunc(char c) {
  if (current().IsPunc(c)) {
    pos_ += 1;
    return true;
  }

  return false;
}

bool Parser::ConsumeIdent(absl::string_view ident) {
  if (current().IsIdent(ident)) {
    pos_ += 1;
    return true;
  }

  return false;
}

// module-map-file:
//   module-declaration*
//
// first(module-declaration) = {explict, framework, module, extern}.
bool Parser::ParseModuleMapFile(ModuleMap* modules) {
  while (current().IsIdentOf({kExplicit, kFramework, kModule, kExtern})) {
    Module module;
    if (!ParseModuleDeclaration(&module)) {
      return false;
    }
    modules->add_module(std::move(module));
  }

  return true;
}

// module-id:
//  identifier ('.' identifier)*
bool Parser::ParseModuleId(string* module_id) {
  string s;
  if (!ParseIdent(&s)) {
    return false;
  }
  *module_id = std::move(s);

  while (current().IsPunc('.')) {
    if (!ConsumePunc('.')) {
      return false;
    }
    *module_id += '.';

    if (!ParseIdent(&s)) {
      return false;
    }
    *module_id += s;
  }

  return true;
}

// module-declaration:
//   explicitopt frameworkopt module module-id attributesopt
//     '{' module-member* '}'
//   extern module module-id string-literal
bool Parser::ParseModuleDeclaration(Module* module_decl) {
  if (current().IsIdentOf({kExplicit, kFramework, kModule})) {
    // explicitopt frameworkopt module module-id attributesopt
    //     '{' module-member* '}'
    module_decl->set_is_explicit(ConsumeIdent(kExplicit));
    module_decl->set_is_framework(ConsumeIdent(kFramework));
    if (!ConsumeIdent(kModule)) {
      return false;
    }
    if (!ParseModuleId(module_decl->mutable_module_id())) {
      return false;
    }
    if (!ParseAttributesOpt(module_decl->mutable_attributes())) {
      return false;
    }
    if (!ConsumePunc('{')) {
      return false;
    }
    if (!ParseModuleMembersOpt(module_decl)) {
      return false;
    }
    if (!ConsumePunc('}')) {
      return false;
    }
    return true;
  }

  // extern module module-id string-literal
  if (current().IsIdent(kExtern)) {
    if (!ConsumeIdent(kExtern)) {
      return false;
    }
    module_decl->set_is_extern(true);
    if (!ConsumeIdent(kModule)) {
      return false;
    }
    if (!ParseModuleId(module_decl->mutable_module_id())) {
      return false;
    }
    if (!ParseString(module_decl->mutable_extern_filename())) {
      return false;
    }
    return true;
  }

  return false;
}

// module-member:
//   requires-declaration          first = requires
//   header-declaration            first = private textual header umbrella
//   exclude umbrella-dir-declaration      first = umbrella
//   submodule-declaration         first = first(module-declaration) |
//                                         first(inferred-submodule-declaration)
//                                       = explicit framework module extern
//   export-declaration            first = export
//   export-as-declaration         first = export_as
//   use-declaration               first = use
//   link-declaration              first = link
//   config-macros-declaration     first = config_macros
//   conflict-declaration          first = conflict
//
// the priblem is "umbrella" and "export" case. We need 2 look-ahead tokens.
// Here is LL(2).
bool Parser::ParseModuleMembersOpt(Module* module_decl) {
  while (true) {
    if (current().IsIdent(kRequires)) {
      if (!ParseRequiresDeclaration(module_decl->mutable_requires())) {
        return false;
      }
      continue;
    }
    if (current().IsIdentOf({kPrivate, kTextual, kHeader, kExclude}) ||
        (current().IsIdent(kUmbrella) && next().IsIdent(kHeader))) {
      Header header;
      if (!ParseHeaderDeclaration(&header)) {
        return false;
      }
      module_decl->add_header(std::move(header));
      continue;
    }
    if (current().IsIdent(kUmbrella) && next().type() == Token::Type::STRING) {
      string name;
      if (!ParseUmbrellaDirDeclaration(&name)) {
        return false;
      }
      module_decl->add_umbrella_dir(name);
      continue;
    }
    if (current().IsIdentOf({kExplicit, kFramework, kModule, kExtern})) {
      Module submodule;
      if (!ParseSubmoduleDeclaration(&submodule)) {
        return false;
      }
      module_decl->add_submodule(std::move(submodule));
      continue;
    }
    if (current().IsIdent(kExport)) {
      string s;
      if (!ParseExportDeclaration(&s)) {
        return false;
      }
      module_decl->add_export(std::move(s));
      continue;
    }
    if (current().IsIdent(kExportAs)) {
      string s;
      if (!ParseExportAsDeclaration(&s)) {
        return false;
      }
      module_decl->add_export_as(std::move(s));
      continue;
    }
    if (current().IsIdent(kUse)) {
      string s;
      if (!ParseUseDeclaration(&s)) {
        return false;
      }
      module_decl->add_use(std::move(s));
      continue;
    }
    if (current().IsIdent(kLink)) {
      Link link;
      if (!ParseLinkDeclaration(&link)) {
        return false;
      }
      module_decl->add_link(std::move(link));
      continue;
    }
    if (current().IsIdent(kConfigMacros)) {
      ConfigMacro config_macro;
      if (!ParseConfigMacrosDeclaration(&config_macro)) {
        return false;
      }
      module_decl->add_config_macros(std::move(config_macro));
      continue;
    }
    if (current().IsIdent(kConflict)) {
      Conflict conflict;
      if (!ParseConflictDeclaration(&conflict)) {
        return false;
      }
      module_decl->add_conflict(std::move(conflict));
      continue;
    }
    // nothing matched.
    return true;
  }
}

// requires-declaration:
//  requires feature-list
bool Parser::ParseRequiresDeclaration(std::vector<Feature>* features) {
  if (!ConsumeIdent(kRequires)) {
    return false;
  }
  if (!ParseFeatureList(features)) {
    return false;
  }
  return true;
}

// feature-list:
//  feature (',' feature)*
bool Parser::ParseFeatureList(std::vector<Feature>* features) {
  Feature f;
  if (!ParseFeature(&f)) {
    return false;
  }
  features->push_back(std::move(f));

  if (ConsumePunc(',')) {
    return ParseFeatureList(features);
  }

  return true;
}

// feature:
//  !opt identifier
bool Parser::ParseFeature(Feature* feature) {
  feature->set_is_positive(!ConsumePunc('!'));
  if (!ParseIdent(feature->mutable_name())) {
    return false;
  }
  return true;
}

// header-declaration:
//   privateopt textualopt header string-literal header-attrsopt
//   umbrella header string-literal header-attrsopt
//   exclude header string-literal header-attrsopt
bool Parser::ParseHeaderDeclaration(Header* header) {
  if (current().IsIdent(kUmbrella)) {
    if (!ConsumeIdent(kUmbrella)) {
      return false;
    }
    header->set_is_umbrella(true);
    if (!ConsumeIdent(kHeader)) {
      return false;
    }
    if (!ParseString(header->mutable_name())) {
      return false;
    }
    if (!ParseHeaderAttrsOpt(header)) {
      return false;
    }
    return true;
  }

  if (current().IsIdent(kExclude)) {
    if (!ConsumeIdent(kExclude)) {
      return false;
    }
    header->set_is_exclude(true);
    if (!ConsumeIdent(kHeader)) {
      return false;
    }
    if (!ParseString(header->mutable_name())) {
      return false;
    }
    if (!ParseHeaderAttrsOpt(header)) {
      return false;
    }
    return true;
  }

  header->set_is_private(ConsumeIdent(kPrivate));
  header->set_is_textual(ConsumeIdent(kTextual));
  if (!ConsumeIdent(kHeader)) {
    return false;
  }
  if (!ParseString(header->mutable_name())) {
    return false;
  }
  if (!ParseHeaderAttrsOpt(header)) {
    return false;
  }
  return true;
}

// header-attrs:
//  '{' header-attr* '}'
bool Parser::ParseHeaderAttrs(Header* header) {
  if (!ConsumePunc('{')) {
    return false;
  }
  while (current().IsIdentOf({kSize, kMtime})) {
    if (!ParseHeaderAttr(header)) {
      return false;
    }
  }
  if (!ConsumePunc('}')) {
    return false;
  }
  return true;
}

bool Parser::ParseHeaderAttrsOpt(Header* header) {
  if (current().IsPunc('{')) {
    return ParseHeaderAttr(header);
  }

  return true;
}

// header-attr:
//   size integer-literal
//   mtime integer-literal
bool Parser::ParseHeaderAttr(Header* header) {
  if (current().IsIdent(kSize)) {
    if (!ConsumeIdent(kSize)) {
      return false;
    }
    if (!ParseInteger(header->mutable_size())) {
      return false;
    }
    return true;
  }

  if (current().IsIdent(kMtime)) {
    if (!ConsumeIdent(kMtime)) {
      return false;
    }
    if (!ParseInteger(header->mutable_mtime())) {
      return false;
    }
    return true;
  }

  return false;
}

// umbrella-dir-declaration:
//   umbrella string-literal
bool Parser::ParseUmbrellaDirDeclaration(string* dirname) {
  if (!ConsumeIdent(kUmbrella)) {
    return false;
  }
  if (!ParseString(dirname)) {
    return false;
  }
  return true;
}

// submodule-declaration:
//   module-declaration
//   inferred-submodule-declaration
//
// inferred-submodule-declaration:
//   explicitopt frameworkopt module '*' attributesopt '{'
//     inferred-submodule-member* '}'
//
// first(module-declaration) = {explicit, framework, module, extern}
// first(inferred-submodule-declaration) = {explicit, framework, module}
// So, we cannot determine to which branch we should go.
// The difference is module-id or '*', so LL(4) here.
bool Parser::ParseSubmoduleDeclaration(Module* module_decl) {
  if (current().IsIdent(kExtern)) {
    module_decl->set_is_extern(true);
    return ParseModuleDeclaration(module_decl);
  }

  module_decl->set_is_explicit(ConsumeIdent(kExplicit));
  module_decl->set_is_framework(ConsumeIdent(kFramework));
  if (!ConsumeIdent(kModule)) {
    return false;
  }

  if (current().IsPunc('*')) {
    // inferred-submodule-declaration

    module_decl->set_is_inferred_submodule(true);

    if (!ConsumePunc('*')) {
      return false;
    }
    module_decl->set_module_id("*");
    if (!ParseAttributesOpt(module_decl->mutable_attributes())) {
      return false;
    }

    if (!ConsumePunc('{')) {
      return false;
    }
    // first(inferred-submodule-member) = "export"
    while (current().IsIdent(kExport)) {
      if (!ParseInferredSubmoduleMember(module_decl)) {
        return false;
      }
    }
    if (!ConsumePunc('}')) {
      return false;
    }

    return true;
  }

  // module-declaration
  if (!ParseModuleId(module_decl->mutable_module_id())) {
    return false;
  }
  if (!ParseAttributesOpt(module_decl->mutable_attributes())) {
    return false;
  }
  if (!ConsumePunc('{')) {
    return false;
  }
  if (!ParseModuleMembersOpt(module_decl)) {
    return false;
  }
  if (!ConsumePunc('}')) {
    return false;
  }
  return true;
}

// inferred-submodule-member:
//  export '*'
bool Parser::ParseInferredSubmoduleMember(Module* module_decl) {
  if (!ConsumeIdent(kExport)) {
    return false;
  }
  if (!ConsumePunc('*')) {
    return false;
  }

  module_decl->set_has_inferfered_submodule_member(true);
  return true;
}

// export-declaration:
//  export wildcard-module-id
bool Parser::ParseExportDeclaration(string* module_id) {
  if (!ConsumeIdent(kExport)) {
    return false;
  }

  if (!ParseWildcardModuleId(module_id)) {
    return false;
  }

  return true;
}

// wildcard-module-id:
//   identifier
//   '*'
//   identifier '.' wildcard-module-id
bool Parser::ParseWildcardModuleId(string* module_id) {
  if (ConsumePunc('*')) {
    *module_id += "*";
    return true;
  }

  string s;
  if (!ParseIdent(&s)) {
    return false;
  }
  *module_id += s;

  if (ConsumePunc('.')) {
    *module_id += ".";
    return ParseWildcardModuleId(module_id);
  }

  return true;
}

// export-as-declaration:
//   export_as identifier
bool Parser::ParseExportAsDeclaration(string* export_as) {
  if (!ConsumeIdent(kExportAs)) {
    return false;
  }
  if (!ParseIdent(export_as)) {
    return false;
  }
  return true;
}

// use-declaration:
//  use module-id
bool Parser::ParseUseDeclaration(string* module_id) {
  if (!ConsumeIdent(kUse)) {
    return false;
  }

  if (!ParseIdent(module_id)) {
    return false;
  }

  return true;
}

// link-declaration:
//  link frameworkopt string-literal
bool Parser::ParseLinkDeclaration(Link* link) {
  if (!ConsumeIdent(kLink)) {
    return false;
  }

  link->is_framework_ = ConsumeIdent(kFramework);
  if (!ParseString(&link->name_)) {
    return false;
  }

  return true;
}

// config-macros-declaration:
//  config_macros attributesopt config-macro-listopt
bool Parser::ParseConfigMacrosDeclaration(ConfigMacro* config_macro) {
  if (!ConsumeIdent(kConfigMacros)) {
    return false;
  }

  //  first(attributes) = '['
  if (current().IsPunc('[')) {
    if (!ParseAttributes(config_macro->mutable_attributes())) {
      return false;
    }
  }

  // first(config-macro-list) = ident
  if (current().type() == Token::Type::IDENT) {
    if (!ParseConfigMacroList(config_macro->mutable_macros())) {
      return false;
    }
  }

  return true;
}

// config-macro-list:
//  identifier (',' identifier)*
bool Parser::ParseConfigMacroList(std::vector<string>* names) {
  string s;
  if (!ParseIdent(&s)) {
    return false;
  }
  names->push_back(std::move(s));

  while (current().IsPunc(',')) {
    if (!ConsumePunc(',')) {
      return false;
    }
    if (!ParseIdent(&s)) {
      return false;
    }
    names->push_back(std::move(s));
  }

  return true;
}

// conflict-declaration:
//   conflict module-id ',' string-literal
bool Parser::ParseConflictDeclaration(Conflict* conflict) {
  if (!ConsumeIdent(kConflict)) {
    return false;
  }

  if (!ParseModuleId(&conflict->module_id_)) {
    return false;
  }
  if (!ConsumePunc(',')) {
    return false;
  }
  if (!ParseString(&conflict->reason_)) {
    return false;
  }

  return true;
}

// attributes:
//  attribute attributesopt
//
//  first(attributes) = '['
bool Parser::ParseAttributes(std::vector<string>* attributes) {
  string s;
  if (!ParseAttribute(&s)) {
    return false;
  }
  attributes->push_back(std::move(s));

  while (current().IsPunc('[')) {
    if (!ParseAttribute(&s)) {
      return false;
    }
    attributes->push_back(std::move(s));
  }

  return true;
}

bool Parser::ParseAttributesOpt(std::vector<string>* attributes) {
  if (current().IsPunc('[')) {
    return ParseAttributes(attributes);
  }

  return true;
}

// attribute:
//  '[' identifier ']'
bool Parser::ParseAttribute(string* s) {
  if (!ConsumePunc('[')) {
    return false;
  }
  if (!ParseIdent(s)) {
    return false;
  }
  if (!ConsumePunc(']')) {
    return false;
  }

  return true;
}

}  // namespace modulemap
}  // namespace devtools_goma
