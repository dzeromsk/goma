// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_PARSER_H_

#include <memory>
#include <string>

#include "absl/strings/string_view.h"
#include "content.h"
#include "cpp_directive.h"
#include "cpp_input_stream.h"
#include "cpp_tokenizer.h"

using std::string;

namespace devtools_goma {

class CppDirectiveParser {
 public:
  CppDirectiveParser() = default;

  CppDirectiveParser(const CppDirectiveParser&) = delete;
  void operator=(const CppDirectiveParser&) = delete;

  static SharedCppDirectives ParseFromContent(const Content& content,
                                              const string& filename);
  static SharedCppDirectives ParseFromString(const string& string_content,
                                             const string& filename);

  bool Parse(const Content& content,
             const string& filename,
             CppDirectiveList* result);

  bool has_unknown_directives() const { return has_unknown_directives_; }

 private:
  std::unique_ptr<CppDirective> ParseDirective(absl::string_view directive,
                                               CppInputStream* stream);

  bool has_unknown_directives_ = false;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_DIRECTIVE_PARSER_H_
