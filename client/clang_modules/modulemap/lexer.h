// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_LEXER_H_
#define DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_LEXER_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "client/content.h"
#include "glog/logging.h"
#include "token.h"

namespace devtools_goma {
namespace modulemap {

// Lexer is a lexer for modulemap file.
//
// Basic usage is just to run Lexer::Run().
//
// TODO: This lexer doesn't support line-break with '\'.
class Lexer {
 public:
  // Runs lexing for |content|. Token list is saved to |tokens|.
  // Returns true if succeeded, false otherwise.
  static bool Run(const Content& content, std::vector<Token>* tokens);

 private:
  // |content| should be alive while Lexer is alive.
  explicit Lexer(const Content* content)
      : content_(content), pos_(content->buf()) {}

  // Returns next token.
  // END token will be returned if no more token exists.
  // INVALID token will be returned if an error happened while reading tokens.
  Token Next();

  void SkipWhitespaces();
  void SkipUntilNextLine();
  // Skips until `*/` comes. If found, true is returned.
  // If not found until EOF, false is returned.
  bool SkipBlockComment();

  // Returns string_view of the buffer we have not consumed.
  absl::string_view rest_view() const {
    return absl::string_view(pos_, content_->buf_end() - pos_);
  }

  const Content* content_;
  const char* pos_;
};

}  // namespace modulemap
}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_LEXER_H_
