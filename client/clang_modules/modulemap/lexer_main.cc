// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A simple program to runs modulemap lexer and shows the result.

#include <iostream>
#include <string>

#include "client/content.h"
#include "lexer.h"
#include "token.h"

using devtools_goma::Content;
using devtools_goma::modulemap::Lexer;
using devtools_goma::modulemap::Token;
using std::string;

int main(int argc, char* argv[]) {
  if (argc < 2) {
    return 1;
  }

  string path = argv[1];

  std::unique_ptr<Content> content(Content::CreateFromFile(path));
  if (!content) {
    LOG(ERROR) << "failed to open/read " << path;
    return 1;
  }

  std::vector<Token> tokens;
  if (!Lexer::Run(*content, &tokens)) {
    return 1;
  }

  for (const auto& token : tokens) {
    std::cout << token << std::endl;
  }

  return 0;
}
