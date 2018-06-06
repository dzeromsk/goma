// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CPP_DIRECTIVE_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_CPP_DIRECTIVE_PARSER_H_

#include <memory>

#include "content.h"
#include "cpp_directive.h"
#include "cpp_tokenizer.h"

namespace devtools_goma {

class CppDirectiveParser {
 public:
  CppDirectiveParser() = default;

  CppDirectiveParser(const CppDirectiveParser&) = delete;
  void operator=(const CppDirectiveParser&) = delete;

  static SharedCppDirectives ParseFromContent(const Content& content);
  static SharedCppDirectives ParseFromString(const string& string_content);

  bool Parse(const Content& content, CppDirectiveList* result);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CPP_DIRECTIVE_PARSER_H_
