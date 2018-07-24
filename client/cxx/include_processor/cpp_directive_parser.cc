// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_directive_parser.h"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

#include "absl/container/inlined_vector.h"
#include "absl/strings/ascii.h"
#include "compiler_specific.h"
#include "cpp_token.h"
#include "cpp_tokenizer.h"
#include "directive_filter.h"
#include "glog/logging.h"

using std::string;

namespace devtools_goma {

namespace {

bool ReadIdent(CppInputStream* stream, string* ident, string* error_reason) {
  CppToken token;
  if (!CppTokenizer::NextTokenFrom(stream, true, &token, error_reason)) {
    *error_reason = "no token found";
    return false;
  }

  if (token.type != CppToken::IDENTIFIER) {
    *error_reason = "ident is expected, but not";
    return false;
  }

  *ident = token.string_value;
  return true;
}

std::vector<CppToken> ReadTokens(CppInputStream* stream, bool skip_spaces) {
  std::vector<CppToken> result;

  // Note: first space is always skipped.
  CppToken token;
  string error_reason;
  if (!CppTokenizer::NextTokenFrom(stream, true, &token, &error_reason)) {
    LOG(ERROR) << error_reason;
    return result;
  }

  while (token.type != CppToken::END && token.type != CppToken::NEWLINE) {
    result.push_back(std::move(token));
    if (!CppTokenizer::NextTokenFrom(stream, skip_spaces,
                                     &token, &error_reason)) {
      LOG(ERROR) << error_reason;
      break;
    }
  }

  return result;
}

CppToken NextToken(CppInputStream* stream, bool skip_spaces) {
  string error_reason;
  CppToken token;
  if (!CppTokenizer::NextTokenFrom(stream, skip_spaces,
                                   &token, &error_reason)) {
    return CppToken(CppToken::END);
  }

  return token;
}

using SmallCppTokenVector = absl::InlinedVector<CppToken, 16>;

void TrimTokenSpace(SmallCppTokenVector* tokens) {
  while (!tokens->empty() && tokens->back().type == CppToken::SPACE) {
    tokens->pop_back();
  }
}

std::unique_ptr<CppDirective> ReadObjectMacro(
    const string& name, CppInputStream* stream) {
  SmallCppTokenVector replacement;

  CppToken token = NextToken(stream, true);
  while (token.type != CppToken::NEWLINE && token.type != CppToken::END) {
    // Remove contiguous spaces (i.e. '   ' => ' ')
    // Remove preceding spaces for ## (i.e. ' ##' => '##')
    if (token.type == CppToken::SPACE ||
        token.type == CppToken::DOUBLESHARP) {
      TrimTokenSpace(&replacement);
    }

    bool is_double_sharp = token.type == CppToken::DOUBLESHARP;
    replacement.push_back(std::move(token));
    // Remove trailing spaces for ## (i.e. '## ' => '##')
    token = NextToken(stream, is_double_sharp);
  }

  TrimTokenSpace(&replacement);

  return std::unique_ptr<CppDirective>(new CppDirectiveDefine(
      name,
      std::vector<CppToken>(std::make_move_iterator(replacement.begin()),
                            std::make_move_iterator(replacement.end()))));
}

std::unique_ptr<CppDirective> ReadFunctionMacro(const string& name,
                                                CppInputStream* stream) {
  std::unordered_map<string, size_t> params;
  size_t param_index = 0;
  bool is_vararg = false;
  for (;;) {
    CppToken token = NextToken(stream, true);
    if (token.type == CppToken::NEWLINE || token.type == CppToken::END) {
      return CppDirective::Error("missing ')' in the macro parameter list");
    }
    if (token.type == CppToken::IDENTIFIER) {
      if (!params.insert(std::make_pair(token.string_value,
                                        param_index)).second) {
        return CppDirective::Error("duplicate macro parameter ",
                                   token.string_value);
      }
      param_index++;
      token = NextToken(stream, true);
      if (token.IsPuncChar(',')) {
        continue;
      }
      if (token.IsPuncChar(')')) {
        break;
      }
    } else if (token.type == CppToken::TRIPLEDOT) {
      is_vararg = true;
      token = NextToken(stream, true);
      if (!token.IsPuncChar(')')) {
        return CppDirective::Error(
            "vararg must be the last of ""the macro parameter list");
      }
      break;
    } else if (token.IsPuncChar(')')) {
      break;
    }
    return CppDirective::Error("invalid preprocessing macro arg token ",
                               token.DebugString());
  }

  SmallCppTokenVector replacement;

  CppToken token = NextToken(stream, true);
  while (token.type != CppToken::NEWLINE && token.type != CppToken::END) {
    if (token.type == CppToken::IDENTIFIER) {
      auto iter = params.find(token.string_value);
      if (iter != params.end()) {
        token.MakeMacroParam(iter->second);
      } else if (token.string_value == "__VA_ARGS__" && is_vararg) {
        // __VA_ARGS__ is valid only for variadic template.
        token.MakeMacroParamVaArgs(params.size());
      } else if (token.string_value == "__VA_OPT__" &&
                 (is_vararg || params.size() > 0)) {
        // __VA_OPT__ is valid only for variadic template.
        // If __VA_OPT__ is used in non variadic template: (as of 2018-07-13)
        //   1. clang preserves __VA_OPT__ if argument size is 0.
        //   2. In the other cases, it converts to empty token.
        token.MakeMacroParamVaOpt();
      }
    }

    // Remove contiguous spaces (i.e. '   ' => ' ')
    // Remove preceding spaces for ## (i.e. ' ##' => '##')
    if (token.type == CppToken::SPACE ||
        token.type == CppToken::DOUBLESHARP) {
      TrimTokenSpace(&replacement);
    }

    bool is_double_sharp = token.type == CppToken::DOUBLESHARP;
    replacement.push_back(std::move(token));
    // Remove trailing spaces for ## (i.e. '## ' => '##')
    token = NextToken(stream, is_double_sharp);
  }

  TrimTokenSpace(&replacement);
  return std::unique_ptr<CppDirective>(new CppDirectiveDefine(
      std::move(name), params.size(), is_vararg,
      std::vector<CppToken>(std::make_move_iterator(replacement.begin()),
                            std::make_move_iterator(replacement.end()))));
}

// ----------------------------------------------------------------------

template<typename CppDirectiveIncludeCtor>
std::unique_ptr<CppDirective> ParseInclude(CppInputStream* stream) {
  stream->SkipWhiteSpaces();
  int c = stream->GetChar();
  if (c == EOF) {
    return CppDirective::Error("#include expects \"filename\" or <filename>");
  }

  if (c == '<') {
    string path;
    string error_reason;
    if (!CppTokenizer::ReadStringUntilDelimiter(stream, &path,
                                                '>', &error_reason)) {
      return CppDirective::Error(error_reason);
    }

    return std::unique_ptr<CppDirective>(new CppDirectiveIncludeCtor(c, path));
  }
  if (c == '"') {
    string path;
    string error_reason;
    if (!CppTokenizer::ReadStringUntilDelimiter(stream, &path,
                                                '"', &error_reason)) {
      return CppDirective::Error(error_reason);
    }
    return std::unique_ptr<CppDirective>(new CppDirectiveIncludeCtor(c, path));
  }
  stream->UngetChar(c);

  // Include path is neither <filepath> nor "filepath".
  // Keep tokens as is.
  std::vector<CppToken> tokens = ReadTokens(stream, false);
  return std::unique_ptr<CppDirective>(
      new CppDirectiveIncludeCtor(std::move(tokens)));
}

std::unique_ptr<CppDirective> ParseDefine(CppInputStream* stream) {
  CppToken name = NextToken(stream, true);
  if (name.type != CppToken::IDENTIFIER) {
    return CppDirective::Error("invalid preprocessing macro name token: ",
                               name.DebugString());
  }

  CppToken token = NextToken(stream, false);
  if (token.IsPuncChar('(')) {
    return ReadFunctionMacro(name.string_value, stream);
  }

  if (token.type == CppToken::NEWLINE || token.type == CppToken::END) {
    // Token::END. name only macro.
    return std::unique_ptr<CppDirective>(
        new CppDirectiveDefine(name.string_value, std::vector<CppToken>()));
  }

  // here, object macro.
  if (token.type != CppToken::SPACE) {
    return CppDirective::Error("missing whitespace after macro name",
                               token.DebugString());
  }

  return ReadObjectMacro(name.string_value, stream);
}

// Parse undef, and return token.
std::unique_ptr<CppDirective> ParseUndef(CppInputStream* stream) {
  string ident;
  string error_reason;
  if (!ReadIdent(stream, &ident, &error_reason)) {
    return CppDirective::Error("failed to parse #undef: " + error_reason);
  }

  return std::unique_ptr<CppDirective>(new CppDirectiveUndef(std::move(ident)));
}

std::unique_ptr<CppDirective> ParseIfdef(CppInputStream* stream) {
  string ident;
  string error_reason;
  if (!ReadIdent(stream, &ident, &error_reason)) {
    return CppDirective::Error("failed to parse #ifdef: " + error_reason);
  }

  return std::unique_ptr<CppDirective>(new CppDirectiveIfdef(std::move(ident)));
}

// Parse undef, and return token.
std::unique_ptr<CppDirective> ParseIfndef(CppInputStream* stream) {
  string ident;
  string error_reason;
  if (!ReadIdent(stream, &ident, &error_reason)) {
    return CppDirective::Error("failed to parse #ifndef: " + error_reason);
  }

  return std::unique_ptr<CppDirective>(
      new CppDirectiveIfndef(std::move(ident)));
}

std::unique_ptr<CppDirective> ParseIf(CppInputStream* stream) {
  // Since when evaluating #if, all spaces are skipped.
  // So let's skip spaces here, too.
  std::vector<CppToken> tokens = ReadTokens(stream, true);
  if (tokens.empty()) {
    return CppDirective::Error("faield to parse #if: no conditions");
  }
  return std::unique_ptr<CppDirective>(new CppDirectiveIf(std::move(tokens)));
}

std::unique_ptr<CppDirective> ParseElse(CppInputStream* stream ALLOW_UNUSED) {
  // just skip
  return std::unique_ptr<CppDirective>(new CppDirectiveElse());
}

std::unique_ptr<CppDirective> ParseEndif(CppInputStream* stream ALLOW_UNUSED) {
  // just skip
  return std::unique_ptr<CppDirective>(new CppDirectiveEndif());
}

std::unique_ptr<CppDirective> ParseElif(CppInputStream* stream) {
  // Since when evaluating #elif, all spaces are skipped.
  // So let's skip spaces here, too.
  std::vector<CppToken> tokens = ReadTokens(stream, true);
  if (tokens.empty()) {
    return CppDirective::Error("faield to parse #elif: no conditions");
  }
  return std::unique_ptr<CppDirective>(new CppDirectiveElif(std::move(tokens)));
}

std::unique_ptr<CppDirective> ParsePragma(CppInputStream* stream) {
  CppToken token(NextToken(stream, true));
  if (token.type == CppToken::IDENTIFIER && token.string_value == "once") {
    return std::unique_ptr<CppDirective>(new CppDirectivePragma(true));
  }

  return nullptr;
}

// If ignoreable directive is found, nullptr can be returned.
// For error, CppDirectiveError will be returned.
std::unique_ptr<CppDirective> ParseDirective(absl::string_view directive,
                                             CppInputStream* stream) {
  if (directive == "include") {
    return ParseInclude<CppDirectiveInclude>(stream);
  }
  if (directive == "import") {
    return ParseInclude<CppDirectiveImport>(stream);
  }
  if (directive == "include_next") {
    return ParseInclude<CppDirectiveIncludeNext>(stream);
  }
  if (directive == "define") {
    return ParseDefine(stream);
  }
  if (directive == "undef") {
    return ParseUndef(stream);
  }
  if (directive == "ifdef") {
    return ParseIfdef(stream);
  }
  if (directive == "ifndef") {
    return ParseIfndef(stream);
  }
  if (directive == "if") {
    return ParseIf(stream);
  }
  if (directive == "else") {
    return ParseElse(stream);
  }
  if (directive == "endif") {
    return ParseEndif(stream);
  }
  if (directive == "elif") {
    return ParseElif(stream);
  }
  if (directive == "pragma") {
    return ParsePragma(stream);
  }

  if (directive == "error" || directive == "warning") {
    return nullptr;
  }

  LOG(ERROR) << "unexpected directive_value=" << directive << " in "
             << stream->filename() << " line " << stream->line();
  return nullptr;
}

}  // annoymous namespace

// static
SharedCppDirectives CppDirectiveParser::ParseFromContent(
    const Content& content,
    const string& filename) {
  CppDirectiveList directives;
  if (!CppDirectiveParser().Parse(content, filename, &directives)) {
    return nullptr;
  }

  return std::make_shared<CppDirectiveList>(std::move(directives));
}

// static
SharedCppDirectives CppDirectiveParser::ParseFromString(
    const string& string_content,
    const string& filename) {
  std::unique_ptr<Content> content(Content::CreateFromString(string_content));
  if (!content) {
    return nullptr;
  }

  return ParseFromContent(*content, filename);
}

bool CppDirectiveParser::Parse(const Content& content,
                               const string& filename,
                               CppDirectiveList* result) {
  string error_reason;
  CppInputStream stream(&content, filename);

  CppDirectiveList directives;
  while (CppTokenizer::SkipUntilDirective(&stream, &error_reason)) {
    stream.SkipWhiteSpaces();

    absl::InlinedVector<char, 32> directive;
    while (true) {
      int c = stream.GetCharWithBackslashHandling();
      if (c == EOF) {
        break;
      }
      if (!absl::ascii_isalnum(c) && c != '_') {
        stream.UngetChar(c);
        break;
      }
      directive.push_back(c);
    }

    // found directive.
    if (std::unique_ptr<CppDirective> p = ParseDirective(
            absl::string_view(directive.data(), directive.size()), &stream)) {
      p->set_position(directives.size() + 1);
      directives.push_back(std::move(p));
    }
  }

  if (!error_reason.empty()) {
    LOG(ERROR) << "failed to parse directives: " << error_reason;
    return false;
  }

  *result = std::move(directives);
  return true;
}

}  // namespace devtools_goma
