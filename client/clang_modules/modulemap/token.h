// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_TOKEN_H_
#define DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_TOKEN_H_

#include <initializer_list>
#include <string>

#include "absl/algorithm/algorithm.h"
#include "absl/strings/string_view.h"
#include "client/content.h"
#include "glog/logging.h"

using std::string;

namespace devtools_goma {
namespace modulemap {

class Token {
 public:
  enum class Type {
    IDENT,
    STRING,
    INTEGER,
    PUNC,
    END,
    INVALID,
  };

  // utility constructors. We provide only these ctors to users.
  static Token Ident(string value) {
    return Token(Type::IDENT, std::move(value));
  }
  static Token String(string value) {
    return Token(Type::STRING, std::move(value));
  }
  static Token Integer(string value) {
    return Token(Type::INTEGER, std::move(value));
  }
  static Token Punc(char c) { return Token(Type::PUNC, string(1, c)); }
  static Token End() { return Token(Type::END, string()); }
  static Token Invalid() { return Token(Type::INVALID, string()); }

  friend std::ostream& operator<<(std::ostream& os, const Token& token);

  Type type() const { return type_; }
  const string& value() const { return value_; }

  bool IsIdent(absl::string_view ident) const {
    return type_ == Type::IDENT && value_ == ident;
  }

  // Returns true if type is IDENT and value is one of |list|.
  bool IsIdentOf(std::initializer_list<absl::string_view> list) const {
    return type_ == Type::IDENT &&
           absl::linear_search(list.begin(), list.end(), value_);
  }

  bool IsPunc(char c) const {
    // When type is PUNC, value length must be always 1, since
    // Token with type==Type::PUNC can be constructed with Punc().
    // Here, we lean on the safer side.
    return type_ == Type::PUNC && !value_.empty() && value_[0] == c;
  }

  bool IsInteger(absl::string_view s) const {
    return type_ == Type::INTEGER && value_ == s;
  }

  bool IsString(absl::string_view s) const {
    return type_ == Type::STRING && value_ == s;
  }

 private:
  Token(Type type, string value) : type_(type), value_(std::move(value)) {}

  Type type_;
  string value_;
};

}  // namespace modulemap
}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_TOKEN_H_
