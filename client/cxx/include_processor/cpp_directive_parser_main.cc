// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>

#include "cpp_directive_parser.h"
#include "cpp_tokenizer.h"
#include "goma_init.h"

// Parses a cpp source and shows what directives are used.

int main(int argc, char *argv[], const char** envp) {
  using devtools_goma::Content;
  using devtools_goma::CppDirectiveList;
  using devtools_goma::CppDirectiveParser;
  using devtools_goma::CppTokenizer;
  using devtools_goma::SharedCppDirectives;

  devtools_goma::Init(argc, argv, envp);
  devtools_goma::InitLogging(argv[0]);

  if (argc < 2) {
    std::cerr << "Usage:" << std::endl
              << "  " << argv[0] << " <filename>" << std::endl;
    return 1;
  }

  std::unique_ptr<Content> content = Content::CreateFromFile(argv[1]);
  if (!content) {
    std::cerr << "failed to read file: " << argv[1] << std::endl;
    return 1;
  }

  SharedCppDirectives directives =
      CppDirectiveParser::ParseFromContent(*content, argv[1]);
  if (!directives) {
    std::cerr << "failed to parse" << std::endl;
    return 1;
  }

  for (const auto& directive : *directives) {
    std::cout << directive->DebugString() << std::endl;
  }

  return 0;
}
