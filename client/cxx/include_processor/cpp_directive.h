// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_H_

#include <memory>
#include <string>
#include <vector>

#include "cpp_macro.h"
#include "cpp_token.h"

using std::string;

namespace devtools_goma {

// must align with CppParser::kDirectiveTable.
enum class CppDirectiveType {
  DIRECTIVE_INCLUDE,
  DIRECTIVE_IMPORT,
  DIRECTIVE_INCLUDE_NEXT,
  DIRECTIVE_DEFINE,
  DIRECTIVE_UNDEF,
  DIRECTIVE_IFDEF,
  DIRECTIVE_IFNDEF,
  DIRECTIVE_IF,
  DIRECTIVE_ELSE,
  DIRECTIVE_ENDIF,
  DIRECTIVE_ELIF,
  DIRECTIVE_PRAGMA,

  DIRECTIVE_ERROR,  // If an error encountered, use this instead.
};

const int kCppDirectiveTypeSize =
    static_cast<int>(CppDirectiveType::DIRECTIVE_ERROR) + 1;

const char* CppDirectiveTypeToString(CppDirectiveType type);

// CppDirective represents each directive (e.g. #if, #define, ...).
// For each type of directive, there is one derived class (e.g. CppDirectiveIf).
class CppDirective {
 public:
  virtual ~CppDirective() {}

  CppDirectiveType type() const { return directive_type_; }
  int position() const { return position_; }

  // Returns directive type as string.
  // e.g. "if", "else", "define".
  const char* DirectiveTypeName() const {
    return CppDirectiveTypeToString(type());
  }

  // Returns human readable directive for debugging purpose.
  virtual string DebugString() const = 0;

  // utility function to create Error type.
  static std::unique_ptr<CppDirective> Error(string reason);
  static std::unique_ptr<CppDirective> Error(string reason, string arg);

 protected:
  explicit CppDirective(CppDirectiveType directive_type)
      : directive_type_(directive_type),
        position_(-1) {
  }

 private:
  friend class CppDirectiveParser;

  // CppDirectiveParser can set position.
  void set_position(int pos) { position_ = pos; }

  const CppDirectiveType directive_type_;
  int position_;
};

// base class of include, include_next and import.
class CppDirectiveIncludeBase : public CppDirective {
 public:
  ~CppDirectiveIncludeBase() override {}

  char delimiter() const { return delimiter_; }
  const string& filename() const {
    // valid only if delimiter is '<' or '"'.
    DCHECK(delimiter_ == '<' || delimiter_ == '"') << delimiter_;
    return filename_;
  }
  const std::vector<CppToken>& tokens() const {
    // valid only if delimiter is ' '.
    DCHECK(delimiter_ == ' ') << delimiter_;
    return tokens_;
  }

 protected:
  CppDirectiveIncludeBase(CppDirectiveType type,
                          char delimiter, string filename)
      : CppDirective(type),
        delimiter_(delimiter),
        filename_(std::move(filename)) {
    DCHECK(delimiter == '<' || delimiter == '"') << delimiter;
  }
  CppDirectiveIncludeBase(CppDirectiveType type, std::vector<CppToken> tokens)
      : CppDirective(type),
        delimiter_(' '),
        tokens_(std::move(tokens)) {
  }

  string DebugString() const override;

 private:
  const char delimiter_; // one of '<', '"', or ' '.
  const string filename_;

  const std::vector<CppToken> tokens_;
};

// ----------------------------------------------------------------------

class CppDirectiveInclude : public CppDirectiveIncludeBase {
 public:
  CppDirectiveInclude(char delimiter, string filename)
      : CppDirectiveIncludeBase(CppDirectiveType::DIRECTIVE_INCLUDE,
                                delimiter,
                                std::move(filename)) {
  }
  explicit CppDirectiveInclude(std::vector<CppToken> tokens)
      : CppDirectiveIncludeBase(CppDirectiveType::DIRECTIVE_INCLUDE,
                                std::move(tokens)) {
  }
  ~CppDirectiveInclude() override {}
};

// ----------------------------------------------------------------------

class CppDirectiveImport : public CppDirectiveIncludeBase {
 public:
  CppDirectiveImport(char delimiter, string filename)
      : CppDirectiveIncludeBase(CppDirectiveType::DIRECTIVE_IMPORT,
                                delimiter,
                                std::move(filename)) {
  }
  explicit CppDirectiveImport(std::vector<CppToken> tokens)
      : CppDirectiveIncludeBase(CppDirectiveType::DIRECTIVE_IMPORT,
                                std::move(tokens)) {
  }
  ~CppDirectiveImport() override {}
};

// ----------------------------------------------------------------------

class CppDirectiveIncludeNext : public CppDirectiveIncludeBase {
 public:
  CppDirectiveIncludeNext(char delimiter, string filename)
      : CppDirectiveIncludeBase(CppDirectiveType::DIRECTIVE_INCLUDE_NEXT,
                                delimiter,
                                std::move(filename)) {
  }
  explicit CppDirectiveIncludeNext(std::vector<CppToken> tokens)
      : CppDirectiveIncludeBase(CppDirectiveType::DIRECTIVE_INCLUDE_NEXT,
                                std::move(tokens)) {
  }
  ~CppDirectiveIncludeNext() override {}
};

// ----------------------------------------------------------------------

class CppDirectiveDefine : public CppDirective {
 public:
  // ObjectMacro
  CppDirectiveDefine(string name, std::vector<CppToken> replacement)
      : CppDirective(CppDirectiveType::DIRECTIVE_DEFINE),
        macro_(new Macro(std::move(name),
                         Macro::OBJ,
                         std::move(replacement),
                         0,
                         false)) {}

  // FunctionMacro
  CppDirectiveDefine(string name,
                     int num_args,
                     bool has_vararg,
                     std::vector<CppToken> replacement)
      : CppDirective(CppDirectiveType::DIRECTIVE_DEFINE),
        macro_(new Macro(std::move(name),
                         Macro::FUNC,
                         std::move(replacement),
                         num_args,
                         has_vararg)) {}
  ~CppDirectiveDefine() override {}

  string DebugString() const override;

  const string& name() const { return macro_->name; }

  bool is_function_macro() const {
    return macro_->type == Macro::FUNC || macro_->type == Macro::CBK_FUNC;
  }
  int num_args() const {
    DCHECK(is_function_macro());
    return macro_->num_args;
  }
  bool has_vararg() const {
    DCHECK(is_function_macro());
    return macro_->is_vararg;
  }
  const std::vector<CppToken>& replacement() const {
    return macro_->replacement;
  }

  const Macro* macro() const { return macro_.get(); }

 private:
  const std::unique_ptr<Macro> macro_;
};

// ----------------------------------------------------------------------

class CppDirectiveUndef : public CppDirective {
 public:
  explicit CppDirectiveUndef(string name)
      : CppDirective(CppDirectiveType::DIRECTIVE_UNDEF),
        name_(std::move(name)) {
  }
  ~CppDirectiveUndef() override {}

  const string& name() const { return name_; }

  string DebugString() const override {
    return "#undef " + name_;
  }

 private:
  const string name_;
};

// ----------------------------------------------------------------------

class CppDirectiveIfdef : public CppDirective {
 public:
  explicit CppDirectiveIfdef(string name)
      : CppDirective(CppDirectiveType::DIRECTIVE_IFDEF),
        name_(std::move(name)) {}
  ~CppDirectiveIfdef() override {}

  const string& name() const { return name_; }

  string DebugString() const override {
    return "#ifdef " + name_;
  }

 private:
  const string name_;
};

// ----------------------------------------------------------------------

class CppDirectiveIfndef : public CppDirective {
 public:
  explicit CppDirectiveIfndef(string name)
      : CppDirective(CppDirectiveType::DIRECTIVE_IFNDEF),
        name_(std::move(name)) {}
  ~CppDirectiveIfndef() override {}

  const string& name() const { return name_; }

  string DebugString() const override {
    return "#ifndef " + name_;
  }

 private:
  const string name_;
};

// ----------------------------------------------------------------------

class CppDirectiveIf : public CppDirective {
 public:
  explicit CppDirectiveIf(std::vector<CppToken> tokens)
      : CppDirective(CppDirectiveType::DIRECTIVE_IF),
        tokens_(std::move(tokens)) {}
  ~CppDirectiveIf() override {}

  const std::vector<CppToken>& tokens() const { return tokens_; }

  string DebugString() const override;

 private:
  const std::vector<CppToken> tokens_;
};

// ----------------------------------------------------------------------

class CppDirectiveElse : public CppDirective {
 public:
  CppDirectiveElse() : CppDirective(CppDirectiveType::DIRECTIVE_ELSE) {}
  ~CppDirectiveElse() override {}

  string DebugString() const override {
    return "#else";
  }
};

// ----------------------------------------------------------------------

class CppDirectiveEndif : public CppDirective {
 public:
  CppDirectiveEndif() : CppDirective(CppDirectiveType::DIRECTIVE_ENDIF) {}
  ~CppDirectiveEndif() override {}

  string DebugString() const override {
    return "#endif";
  }
};

// ----------------------------------------------------------------------

class CppDirectiveElif : public CppDirective {
 public:
  explicit CppDirectiveElif(std::vector<CppToken> tokens)
      : CppDirective(CppDirectiveType::DIRECTIVE_ELIF),
        tokens_(std::move(tokens)) {}
  ~CppDirectiveElif() override {}

  const std::vector<CppToken>& tokens() const { return tokens_; }
  string DebugString() const override;

 private:
  const std::vector<CppToken> tokens_;
};

// ----------------------------------------------------------------------

class CppDirectivePragma : public CppDirective {
 public:
  explicit CppDirectivePragma(bool is_pragma_once)
      : CppDirective(CppDirectiveType::DIRECTIVE_PRAGMA),
        is_pragma_once_(is_pragma_once) {}
  ~CppDirectivePragma() override {}

  bool is_pragma_once() const { return is_pragma_once_; }

  string DebugString() const override {
    if (is_pragma_once()) {
      return "#pragma once";
    }
    return "#pragma <unknown>";
  }

 private:
  const bool is_pragma_once_;
};

// ----------------------------------------------------------------------

// Represents directives that contains an error.
class CppDirectiveError : public CppDirective {
 public:
  explicit CppDirectiveError(string error_reason)
      : CppDirectiveError(std::move(error_reason), "") {}
  CppDirectiveError(string error_reason, string arg)
      : CppDirective(CppDirectiveType::DIRECTIVE_ERROR),
        error_reason_(std::move(error_reason)),
        arg_(std::move(arg)) {
  }
  ~CppDirectiveError() override {}

  string DebugString() const override {
    return "#<error> reason=" + error_reason_ + " arg=" + arg_;
  }

  const string& error_reason() const { return error_reason_; }
  const string& arg() const { return arg_; }

 private:
  const string error_reason_;
  const string arg_;
};

// ----------------------------------------------------------------------

// static conversion function with checking type.
#define DEFINE_CONVERSION_FUNC(T, dir_type)                           \
  inline const T& As ## T(const CppDirective& directive) {            \
    DCHECK(directive.type() == dir_type)                              \
        << "type mismatch:"                                           \
        << " actual=" << CppDirectiveTypeToString(directive.type())   \
        << " expected=" << CppDirectiveTypeToString(dir_type);        \
    return static_cast<const T&>(directive);                          \
  }

  DEFINE_CONVERSION_FUNC(CppDirectiveInclude,
                         CppDirectiveType::DIRECTIVE_INCLUDE);
  DEFINE_CONVERSION_FUNC(CppDirectiveImport,
                         CppDirectiveType::DIRECTIVE_IMPORT);
  DEFINE_CONVERSION_FUNC(CppDirectiveIncludeNext,
                         CppDirectiveType::DIRECTIVE_INCLUDE_NEXT);
  DEFINE_CONVERSION_FUNC(CppDirectiveDefine,
                         CppDirectiveType::DIRECTIVE_DEFINE);
  DEFINE_CONVERSION_FUNC(CppDirectiveUndef,
                         CppDirectiveType::DIRECTIVE_UNDEF);
  DEFINE_CONVERSION_FUNC(CppDirectiveIfdef,
                         CppDirectiveType::DIRECTIVE_IFDEF);
  DEFINE_CONVERSION_FUNC(CppDirectiveIfndef,
                         CppDirectiveType::DIRECTIVE_IFNDEF);
  DEFINE_CONVERSION_FUNC(CppDirectiveIf,
                         CppDirectiveType::DIRECTIVE_IF);
  DEFINE_CONVERSION_FUNC(CppDirectiveElse,
                         CppDirectiveType::DIRECTIVE_ELSE);
  DEFINE_CONVERSION_FUNC(CppDirectiveEndif,
                         CppDirectiveType::DIRECTIVE_ENDIF);
  DEFINE_CONVERSION_FUNC(CppDirectiveElif,
                         CppDirectiveType::DIRECTIVE_ELIF);
  DEFINE_CONVERSION_FUNC(CppDirectivePragma,
                         CppDirectiveType::DIRECTIVE_PRAGMA);
  DEFINE_CONVERSION_FUNC(CppDirectiveError,
                         CppDirectiveType::DIRECTIVE_ERROR);
#undef DEFINE_CONVERSION_FUNC

inline const CppDirectiveIncludeBase& AsCppDirectiveIncludeBase(
    const CppDirective& directive) {
  DCHECK(directive.type() == CppDirectiveType::DIRECTIVE_INCLUDE ||
         directive.type() == CppDirectiveType::DIRECTIVE_IMPORT ||
         directive.type() == CppDirectiveType::DIRECTIVE_INCLUDE_NEXT)
      << CppDirectiveTypeToString(directive.type());
  return static_cast<const CppDirectiveIncludeBase&>(directive);
}

using CppDirectiveList = std::vector<std::unique_ptr<const CppDirective>>;

using SharedCppDirectives = std::shared_ptr<const CppDirectiveList>;

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_H_
