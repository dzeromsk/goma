// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_directive_parser.h"

#include "absl/strings/string_view.h"
#include "basictypes.h"
#include "cpp_parser.h"
#include "gtest/gtest.h"

namespace devtools_goma {

namespace {

SharedCppDirectives Parse(absl::string_view text) {
  std::unique_ptr<Content> content =
      Content::CreateFromBuffer(text.data(), text.size());

  SharedCppDirectives directives(
      CppDirectiveParser::ParseFromContent(*content, "<string>"));
  EXPECT_TRUE(directives != nullptr);
  return directives;
}

// Parse single directive, and returns it.
std::unique_ptr<const CppDirective> ParseSingle(absl::string_view line) {
  // Cannot use the above Parse(), since it cannot be std::move()-ed.

  std::unique_ptr<Content> content =
      Content::CreateFromBuffer(line.data(), line.size());

  CppDirectiveList directives;
  if (!CppDirectiveParser().Parse(*content, "<string>", &directives)) {
    return nullptr;
  }
  if (directives.size() != 1U) {
    return nullptr;
  }
  return std::move(directives[0]);
}

}  // anonymous namespace

class CppDirectiveParserTest : public testing::Test {
 protected:
  CppDirectiveParserTest() {
    // HACK: To initialize tables.
    CppParser parser;
  }
};

TEST_F(CppDirectiveParserTest, Parse) {
  struct TestCase {
    const char* text;
    CppDirectiveType type;
  } testcases[] = {
    { "#include <iostream>", CppDirectiveType::DIRECTIVE_INCLUDE },
    { "#import <iostream>", CppDirectiveType::DIRECTIVE_IMPORT },
    { "#include_next <iostream>", CppDirectiveType::DIRECTIVE_INCLUDE_NEXT },
    { "#define A", CppDirectiveType::DIRECTIVE_DEFINE },
    { "#undef A", CppDirectiveType::DIRECTIVE_UNDEF },
    { "#ifdef A", CppDirectiveType::DIRECTIVE_IFDEF },
    { "#ifndef A", CppDirectiveType::DIRECTIVE_IFNDEF },
    { "#if A", CppDirectiveType::DIRECTIVE_IF },
    { "#else", CppDirectiveType::DIRECTIVE_ELSE },
    { "#endif", CppDirectiveType::DIRECTIVE_ENDIF },
    { "#elif A", CppDirectiveType::DIRECTIVE_ELIF },
    { "#pragma once", CppDirectiveType::DIRECTIVE_PRAGMA },
  };

  // By single line
  for (const auto& tc : testcases) {
    auto d = ParseSingle(tc.text);
    ASSERT_TRUE(d != nullptr);
    EXPECT_EQ(tc.type, d->type());
    EXPECT_EQ(1, d->position());
  }

  // By multiple line (LF case)
  {
    string s;
    for (const auto& tc : testcases) {
      s += tc.text;
      s += "\n";
    }

    auto ds = Parse(s);
    ASSERT_EQ(arraysize(testcases), ds->size());
    for (size_t i = 0; i < ds->size(); ++i) {
      EXPECT_EQ(testcases[i].type, (*ds)[i]->type()) << i;
      EXPECT_EQ(i + 1, (*ds)[i]->position()) << testcases[i].text;
    }
  }

  // By multiple line (CRLF case)
  {
    string s;
    for (const auto& tc : testcases) {
      s += tc.text;
      s += "\r\n";
    }

    auto ds = Parse(s);
    ASSERT_EQ(arraysize(testcases), ds->size());
    for (size_t i = 0; i < ds->size(); ++i) {
      EXPECT_EQ(testcases[i].type, (*ds)[i]->type()) << i;
      EXPECT_EQ(i + 1, (*ds)[i]->position()) << testcases[i].text;
    }
  }
}

TEST_F(CppDirectiveParserTest, Include) {
  {
    auto p = ParseSingle("#include <a.h>");
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE, p->type());
    ASSERT_TRUE(p != nullptr);

    const CppDirectiveInclude& d = AsCppDirectiveInclude(*p);
    EXPECT_EQ('<', d.delimiter());
    EXPECT_EQ("a.h", d.filename());
  }

  {
    auto p = ParseSingle("#include <unicode/ucnv.h>");
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE, p->type());
    ASSERT_TRUE(p != nullptr);

    const CppDirectiveInclude& d = AsCppDirectiveInclude(*p);
    EXPECT_EQ('<', d.delimiter());
    EXPECT_EQ("unicode/ucnv.h", d.filename());
  }

  {
    auto p = ParseSingle("#include \"a.h\"");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE, p->type());

    const CppDirectiveInclude& d = AsCppDirectiveInclude(*p);
    EXPECT_EQ('\"', d.delimiter());
    EXPECT_EQ("a.h", d.filename());
  }

  {
    auto p = ParseSingle("#include \"unicode/ucnv.h\"");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE, p->type());

    const CppDirectiveInclude& d = AsCppDirectiveInclude(*p);
    EXPECT_EQ('\"', d.delimiter());
    EXPECT_EQ("unicode/ucnv.h", d.filename());
  }

  {
    auto p = ParseSingle("#include A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE, p->type());

    const CppDirectiveInclude& d = AsCppDirectiveInclude(*p);
    EXPECT_EQ(' ', d.delimiter());
    ASSERT_EQ(1U, d.tokens().size());
    EXPECT_EQ(CppToken::IDENTIFIER, d.tokens()[0].type);
    EXPECT_EQ("A", d.tokens()[0].string_value);
  }

  // invalid case
  {
    auto p = ParseSingle("#include");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Import) {
  {
    auto p = ParseSingle("#import <a.h>");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IMPORT, p->type());

    const CppDirectiveImport& d = AsCppDirectiveImport(*p);
    EXPECT_EQ('<', d.delimiter());
    EXPECT_EQ("a.h", d.filename());
  }

  {
    auto p = ParseSingle("#import \"a.h\"");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IMPORT, p->type());

    const CppDirectiveImport& d = AsCppDirectiveImport(*p);
    EXPECT_EQ('\"', d.delimiter());
    EXPECT_EQ("a.h", d.filename());
  }

  {
    auto p = ParseSingle("#import A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IMPORT, p->type());

    const CppDirectiveImport& d = AsCppDirectiveImport(*p);
    EXPECT_EQ(' ', d.delimiter());
    ASSERT_EQ(1U, d.tokens().size());
    EXPECT_EQ(CppToken::IDENTIFIER, d.tokens()[0].type);
    EXPECT_EQ("A", d.tokens()[0].string_value);
  }

  // invalid case
  {
    auto p = ParseSingle("#import");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, IncludeNext) {
  {
    auto p = ParseSingle("#include_next <a.h>");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE_NEXT, p->type());

    const CppDirectiveIncludeNext& d = AsCppDirectiveIncludeNext(*p);
    EXPECT_EQ('<', d.delimiter());
    EXPECT_EQ("a.h", d.filename());
  }

  {
    auto p = ParseSingle("#include_next \"a.h\"");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE_NEXT, p->type());

    const CppDirectiveIncludeNext& d = AsCppDirectiveIncludeNext(*p);
    EXPECT_EQ('\"', d.delimiter());
    EXPECT_EQ("a.h", d.filename());
  }

  {
    auto p = ParseSingle("#include_next A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_INCLUDE_NEXT, p->type());

    const CppDirectiveIncludeNext& d = AsCppDirectiveIncludeNext(*p);
    EXPECT_EQ(' ', d.delimiter());
    ASSERT_EQ(1U, d.tokens().size());
    EXPECT_EQ(CppToken::IDENTIFIER, d.tokens()[0].type);
    EXPECT_EQ("A", d.tokens()[0].string_value);
  }

  // invalid case
  {
    auto p = ParseSingle("#include_next");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, IncludeDefine) {
  {
    auto p = ParseSingle("#define A");
    ASSERT_TRUE(p != nullptr);
    ASSERT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, p->type());

    const CppDirectiveDefine& d = AsCppDirectiveDefine(*p);
    EXPECT_EQ("A", d.name());
    EXPECT_FALSE(d.is_function_macro());
    EXPECT_EQ((std::vector<CppToken>()), d.replacement());
  }

  {
    auto p = ParseSingle("#define A B");
    ASSERT_TRUE(p != nullptr);
    ASSERT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, p->type());

    const CppDirectiveDefine& d = AsCppDirectiveDefine(*p);
    EXPECT_EQ("A", d.name());
    EXPECT_FALSE(d.is_function_macro());
    ASSERT_EQ(1U, d.replacement().size());
    EXPECT_EQ(CppToken::IDENTIFIER, d.replacement()[0].type);
    EXPECT_EQ("B", d.replacement()[0].string_value);
  }

  {
    auto p = ParseSingle("#define A() 100");
    ASSERT_TRUE(p != nullptr);
    ASSERT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, p->type())
        << p->DebugString();

    const CppDirectiveDefine& d = AsCppDirectiveDefine(*p);
    EXPECT_EQ("A", d.name());
    EXPECT_TRUE(d.is_function_macro());
    EXPECT_EQ(0, d.num_args());
    EXPECT_FALSE(d.has_vararg());
    ASSERT_EQ(1U, d.replacement().size());
    EXPECT_EQ(CppToken::NUMBER, d.replacement()[0].type);
    EXPECT_EQ("100", d.replacement()[0].string_value);
  }

  {
    auto p = ParseSingle("#define A(x, y) (x) + (y)");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, p->type());

    const CppDirectiveDefine& d = AsCppDirectiveDefine(*p);
    EXPECT_EQ("A", d.name());
    EXPECT_TRUE(d.is_function_macro());
    EXPECT_EQ(2, d.num_args());
    EXPECT_FALSE(d.has_vararg());
    EXPECT_EQ(9U, d.replacement().size());
  }

  {
    auto p = ParseSingle("#define A(x, y) (x) + (y) + z");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, p->type());

    const CppDirectiveDefine& d = AsCppDirectiveDefine(*p);
    EXPECT_EQ("A", d.name());
    EXPECT_TRUE(d.is_function_macro());
    EXPECT_EQ(2, d.num_args());
    EXPECT_FALSE(d.has_vararg());
    EXPECT_EQ(13U, d.replacement().size());
  }

  {
    auto p = ParseSingle("#define A(x, y, ...) (x) + (y) + __VA_ARGS__");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, p->type());

    const CppDirectiveDefine& d = AsCppDirectiveDefine(*p);
    EXPECT_EQ("A", d.name());
    EXPECT_TRUE(d.is_function_macro());
    EXPECT_EQ(2, d.num_args());
    EXPECT_TRUE(d.has_vararg());
    //  0   1   2   3   4   5   6   7   8   9  10  11  12
    // [(] [x] [)] [ ] [+] [ ] [(] [y] [)] [ ] [+] [ ] [__VA_ARGS__]
    ASSERT_EQ(13U, d.replacement().size());

    EXPECT_EQ(CppToken::MACRO_PARAM, d.replacement()[1].type);
    EXPECT_EQ(0U, d.replacement()[1].v.param_index);
    EXPECT_EQ(CppToken::MACRO_PARAM, d.replacement()[7].type);
    EXPECT_EQ(1U, d.replacement()[7].v.param_index);
    EXPECT_EQ(CppToken::MACRO_PARAM_VA_ARGS, d.replacement()[12].type);
    EXPECT_EQ(2U, d.replacement()[12].v.param_index);
  }

  // non variadic macro, but __VA_ARGS__ is used.
  // __VA_ARGS__ must not be treated as MACRO_PARAM_VA_ARGS.
  {
    auto p = ParseSingle("#define A(x, y) (x) + (y) + __VA_ARGS__");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, p->type());

    const CppDirectiveDefine& d = AsCppDirectiveDefine(*p);
    EXPECT_EQ("A", d.name());
    EXPECT_TRUE(d.is_function_macro());
    EXPECT_EQ(2, d.num_args());
    EXPECT_FALSE(d.has_vararg());
    //  0   1   2   3   4   5   6   7   8   9  10  11  12
    // [(] [x] [)] [ ] [+] [ ] [(] [y] [)] [ ] [+] [ ] [__VA_ARGS__]
    ASSERT_EQ(13U, d.replacement().size());

    EXPECT_EQ(CppToken::MACRO_PARAM, d.replacement()[1].type);
    EXPECT_EQ(0U, d.replacement()[1].v.param_index);
    EXPECT_EQ(CppToken::MACRO_PARAM, d.replacement()[7].type);
    EXPECT_EQ(1U, d.replacement()[7].v.param_index);
    EXPECT_EQ(CppToken::IDENTIFIER, d.replacement()[12].type);
  }

  // invalid case
  {
    auto p = ParseSingle("#define");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Undef) {
  {
    auto p = ParseSingle("#undef A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_UNDEF, p->type());

    const CppDirectiveUndef& d = AsCppDirectiveUndef(*p);
    EXPECT_EQ("A", d.name());
  }

  // invalid case
  {
    auto p = ParseSingle("#undef");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Ifdef) {
  {
    auto p = ParseSingle("#ifdef A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFDEF, p->type());

    const CppDirectiveIfdef& d = AsCppDirectiveIfdef(*p);
    EXPECT_EQ("A", d.name());
  }

  // invalid case
  {
    auto p = ParseSingle("#ifdef");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Ifndef) {
  {
    auto p = ParseSingle("#ifndef A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFNDEF, p->type());

    const CppDirectiveIfndef& d = AsCppDirectiveIfndef(*p);
    EXPECT_EQ("A", d.name());
  }

  // invalid case
  {
    auto p = ParseSingle("#ifndef");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, If) {
  {
    auto p = ParseSingle("#if 1 < 2");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, p->type());

    const CppDirectiveIf& d = AsCppDirectiveIf(*p);
    // [1] [<] [2]
    EXPECT_EQ(3, d.tokens().size());
  }

  {
    auto p = ParseSingle("#if defined( FOO)");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, p->type());

    const CppDirectiveIf& d = AsCppDirectiveIf(*p);
    // [defined] [(] [FOO] [)]
    EXPECT_EQ(4, d.tokens().size());
  }

  {
    auto p = ParseSingle("#if A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, p->type());

    const CppDirectiveIf& d = AsCppDirectiveIf(*p);
    EXPECT_EQ(1, d.tokens().size());
  }

  // invalid case
  {
    auto p = ParseSingle("#if");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Else) {
  {
    auto p = ParseSingle("#else");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ELSE, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Endif) {
  {
    auto p = ParseSingle("#endif");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Elif) {
  {
    auto p = ParseSingle("#elif 1 < 2");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ELIF, p->type());

    const CppDirectiveElif& d = AsCppDirectiveElif(*p);
    // [1] [<] [2]
    EXPECT_EQ(3, d.tokens().size());
  }

  {
    auto p = ParseSingle("#elif A");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ELIF, p->type());

    const CppDirectiveElif& d = AsCppDirectiveElif(*p);
    EXPECT_EQ(1, d.tokens().size());
  }

  // invalid case
  {
    auto p = ParseSingle("#elif");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_ERROR, p->type());
  }
}

TEST_F(CppDirectiveParserTest, Pragma) {
  {
    auto p = ParseSingle("#pragma once");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_PRAGMA, p->type());

    const CppDirectivePragma& d = AsCppDirectivePragma(*p);
    EXPECT_TRUE(d.is_pragma_once());
  }

  {
    auto p = ParseSingle("#pragma      once");
    ASSERT_TRUE(p != nullptr);
    EXPECT_EQ(CppDirectiveType::DIRECTIVE_PRAGMA, p->type());

    const CppDirectivePragma& d = AsCppDirectivePragma(*p);
    EXPECT_TRUE(d.is_pragma_once());
  }

  // Only "#pragma once" is parsed. The other pragma is skipped.
  {
    auto p = ParseSingle("#pragma FOO BAR");
    EXPECT_TRUE(p == nullptr);
  }
}

TEST_F(CppDirectiveParserTest, ErrorWarning) {
  // #error and #warning will be removed after parsing.
  auto p = Parse(
      "#define A\n"
      "#warning \"foo\"\n"
      "#error \"bar\"\n"
      "#define B\n");

  ASSERT_EQ(2U, p->size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, (*p)[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, (*p)[1]->type());
}

}  // namespace devtools_goma
