// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lexer.h"

#include <algorithm>

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"

namespace devtools_goma {
namespace modulemap {

// This is not so accurate. Use CppTokenizer or something?
// static
bool Lexer::Run(const Content& content, std::vector<Token>* tokens) {
  Lexer lexer(&content);
  for (Token token = lexer.Next(); token.type() != Token::Type::END;
       token = lexer.Next()) {
    if (token.type() == Token::Type::INVALID) {
      return false;
    }
    tokens->push_back(std::move(token));
  }
  return true;
}

Token Lexer::Next() {
  SkipWhitespaces();

  if (pos_ == content_->buf_end()) {
    return Token::End();
  }

  if (*pos_ == '\"') {
    // string started.
    // TODO: correctly handle backslash escape.
    // e.g. "foo\"bar" should generate string `foo"bar` but now foo\"bar
    ++pos_;
    const char* const begin = pos_;
    while (pos_ != content_->buf_end() && *pos_ != '\"') {
      if (*pos_ == '\\') {
        ++pos_;
        if (pos_ == content_->buf_end()) {
          return Token::Invalid();
        }
      }
      ++pos_;
    }
    if (pos_ == content_->buf_end()) {
      return Token::Invalid();
    }
    return Token::String(string(begin, pos_++));
  }

  if (absl::ascii_isdigit(*pos_)) {
    const char* const begin = pos_;
    const char* const end =
        std::find_if_not(begin, content_->buf_end(), absl::ascii_isdigit);
    pos_ = end;
    return Token::Integer(string(begin, end));
  }

  if (absl::ascii_isalpha(*pos_) || *pos_ == '_') {
    const char* const begin = pos_;
    while (pos_ != content_->buf_end() &&
           (absl::ascii_isalnum(*pos_) || *pos_ == '_')) {
      ++pos_;
    }
    return Token::Ident(string(begin, pos_));
  }

  if (absl::StartsWith(rest_view(), "//")) {
    pos_ += 2;  // skip "//"
    SkipUntilNextLine();
    return Next();
  }

  if (absl::StartsWith(rest_view(), "/*")) {
    pos_ += 2;  // skip "/*"
    if (!SkipBlockComment()) {
      return Token::Invalid();
    }
    return Next();
  }

  // the others are punc (for now).
  return Token::Punc(*pos_++);
}

void Lexer::SkipWhitespaces() {
  pos_ = std::find_if_not(pos_, content_->buf_end(), absl::ascii_isspace);
}

void Lexer::SkipUntilNextLine() {
  pos_ = std::find(pos_, content_->buf_end(), '\n');
  if (pos_ != content_->buf_end()) {
    ++pos_;  // skip '\n'
  }
}

bool Lexer::SkipBlockComment() {
  absl::string_view::size_type count = rest_view().find("*/");
  if (count == absl::string_view::npos) {
    return false;
  }
  pos_ += count + 2;  // +2 to skip "*/"
  return true;
}

}  // namespace modulemap
}  // namespace devtools_goma
