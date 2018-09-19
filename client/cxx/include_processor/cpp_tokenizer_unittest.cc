// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>

#include <gtest/gtest.h>

#include "cpp_tokenizer.h"

namespace devtools_goma {

TEST(CppTokenizerTest, IsAfterEndOfLine) {
  const char* src1 = " #include <iostream>";
  EXPECT_TRUE(CppTokenizer::IsAfterEndOfLine(strchr(src1, '#'), src1));

  const char* src2 = " f(); #include <iostream>";
  EXPECT_FALSE(CppTokenizer::IsAfterEndOfLine(strchr(src2, '#'), src2));

  const char* src3 = " \n #include <iostream>";
  EXPECT_TRUE(CppTokenizer::IsAfterEndOfLine(strchr(src3, '#'), src3));

  const char* src4 = " f(); \n #include <iostream>";
  EXPECT_TRUE(CppTokenizer::IsAfterEndOfLine(strchr(src4, '#'), src4));

  const char* src5 = "  \\\n #include <iostream>";
  EXPECT_TRUE(CppTokenizer::IsAfterEndOfLine(strchr(src5, '#'), src5));

  const char* src6 = " f(); \\\n #include <iostream>";
  EXPECT_FALSE(CppTokenizer::IsAfterEndOfLine(strchr(src6, '#'), src6));

  const char* src7 = " /* foo */  \\\n #include <iostream>";
  EXPECT_TRUE(CppTokenizer::IsAfterEndOfLine(strchr(src7, '#'), src7));

  const char* src8 = " f(); /* foo */ \\\n #include <iostream>";
  EXPECT_FALSE(CppTokenizer::IsAfterEndOfLine(strchr(src8, '#'), src8));

  const char* src9 = " /* foo */ \\\r\n /* foo */  \\\n #include <iostream>";
  EXPECT_TRUE(CppTokenizer::IsAfterEndOfLine(strchr(src9, '#'), src9));

  const char* src10 = "f();/* foo */ \\\r\n /* foo */ \\\n #include <iostream>";
  EXPECT_FALSE(CppTokenizer::IsAfterEndOfLine(strchr(src10, '#'), src10));
}

bool ReadCharLiteral(const string& s, CppToken* token, bool check_end) {
  std::unique_ptr<Content> c(Content::CreateFromString(s));

  CppInputStream stream(c.get(), "<content>");
  CHECK_EQ(stream.GetChar(), '\'');

  return
      CppTokenizer::ReadCharLiteral(&stream, token) &&
      (!check_end || stream.cur() == stream.end());
}

TEST(CppTokenizerTest, ReadCharLiteral) {
  // non-ASCII system is not supported.

  const bool kCHECK_END = true;
  CppToken token;
  EXPECT_TRUE(ReadCharLiteral("' '", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, ' '), token);

  EXPECT_TRUE(ReadCharLiteral("'*'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '*'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\\\'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\\'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\n'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\n'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\0'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\0'), token);

  EXPECT_TRUE(ReadCharLiteral("'A'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, 'A'), token);

  EXPECT_TRUE(ReadCharLiteral("'0'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '0'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\x01'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\x01'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\x2A'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\x2A'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\01'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\01'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\33'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\33'), token);

  EXPECT_TRUE(ReadCharLiteral("'\\377'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '\377'), token);

  EXPECT_TRUE(ReadCharLiteral("'pset'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, 'pset'), token);

  EXPECT_TRUE(ReadCharLiteral("'?*?*'", &token, kCHECK_END));
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, '?*?*'), token);

  EXPECT_TRUE(ReadCharLiteral("'TO'", &token, kCHECK_END));
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmultichar"
#endif  // __clang__
  // warning: multi-caracter character constant [-Wmultichar]
  EXPECT_EQ(CppToken(CppToken::CHAR_LITERAL, 'TO'), token);
#ifdef __clang__
#pragma clang diagnostic pop
#endif  // __clang__

  EXPECT_FALSE(ReadCharLiteral("''", &token, !kCHECK_END));

  EXPECT_FALSE(ReadCharLiteral("'", &token, !kCHECK_END));

  EXPECT_FALSE(ReadCharLiteral("'\\", &token, !kCHECK_END));

  EXPECT_FALSE(ReadCharLiteral("'0", &token, !kCHECK_END));
}

TEST(CppTokenizerTest, TokenizeDefineString) {
  CppToken t;
  string error;

  std::unique_ptr<Content> content(Content::CreateFromString(
      "#define KOTORI \"piyo\\\"piyo\""));
  CppInputStream stream(content.get(), "<content>");

  EXPECT_TRUE(
      CppTokenizer::NextTokenFrom(&stream, SpaceHandling::kSkip, &t, &error));
  EXPECT_EQ(t.type, CppToken::SHARP);

  EXPECT_TRUE(
      CppTokenizer::NextTokenFrom(&stream, SpaceHandling::kSkip, &t, &error));
  EXPECT_EQ(t.type, CppToken::IDENTIFIER);
  EXPECT_EQ(t.string_value, "define");

  EXPECT_TRUE(
      CppTokenizer::NextTokenFrom(&stream, SpaceHandling::kSkip, &t, &error));
  EXPECT_EQ(t.type, CppToken::IDENTIFIER);
  EXPECT_EQ(t.string_value, "KOTORI");

  EXPECT_TRUE(
      CppTokenizer::NextTokenFrom(&stream, SpaceHandling::kSkip, &t, &error));
  EXPECT_EQ(t.type, CppToken::STRING);
  EXPECT_EQ(t.string_value, "piyo\\\"piyo");

  EXPECT_TRUE(
      CppTokenizer::NextTokenFrom(&stream, SpaceHandling::kSkip, &t, &error));
  EXPECT_EQ(t.type, CppToken::END);
}

TEST(CppTokenizerTest, TokenizeIdentifier) {
  const string content = "A B $X X$ $X$ __$X";

  ArrayTokenList tokens;
  EXPECT_TRUE(
      CppTokenizer::TokenizeAll(content, SpaceHandling::kSkip, &tokens));
  // [A][B][$X][X$][$X$][__$X]
  ASSERT_EQ(6U, tokens.size());

  tokens[0].IsIdentifier("A");
  tokens[1].IsIdentifier("B");
  tokens[2].IsIdentifier("$X");
  tokens[3].IsIdentifier("X$");
  tokens[4].IsIdentifier("$X$");
  tokens[5].IsIdentifier("__$X");
}

TEST(CppTokenizerTest, TokenizeAll) {
  const string content = "A B 1+2";

  ArrayTokenList tokens_wos;
  EXPECT_TRUE(
      CppTokenizer::TokenizeAll(content, SpaceHandling::kSkip, &tokens_wos));
  // [A][B][1][+][2]
  EXPECT_EQ(5U, tokens_wos.size());

  ArrayTokenList tokens_ws;
  EXPECT_TRUE(
      CppTokenizer::TokenizeAll(content, SpaceHandling::kKeep, &tokens_ws));
  // [A][ ][B][ ][1][+][2]
  EXPECT_EQ(7U, tokens_ws.size());
}

TEST(CppTokenizerTest, IntegerSuffixes) {
  EXPECT_TRUE(CppTokenizer::IsValidIntegerSuffix("u"));
  EXPECT_TRUE(CppTokenizer::IsValidIntegerSuffix("l"));
  EXPECT_TRUE(CppTokenizer::IsValidIntegerSuffix("ul"));
  EXPECT_TRUE(CppTokenizer::IsValidIntegerSuffix("lu"));
  EXPECT_TRUE(CppTokenizer::IsValidIntegerSuffix("ll"));
  EXPECT_TRUE(CppTokenizer::IsValidIntegerSuffix("ull"));
  EXPECT_TRUE(CppTokenizer::IsValidIntegerSuffix("llu"));

  EXPECT_FALSE(CppTokenizer::IsValidIntegerSuffix(""));
  EXPECT_FALSE(CppTokenizer::IsValidIntegerSuffix("lul"));
}

TEST(CppTokenizerTest, TypeFrom) {
  std::vector<std::vector<CppToken::Type>> expected(
      128, std::vector<CppToken::Type>(128, CppToken::PUNCTUATOR));

#define UC(c) static_cast<unsigned char>(c)
  expected[UC('=')][UC('=')] = CppToken::EQ;
  expected[UC('!')][UC('=')] = CppToken::NE;
  expected[UC('>')][UC('=')] = CppToken::GE;
  expected[UC('<')][UC('=')] = CppToken::LE;
  expected[UC('&')][UC('&')] = CppToken::LAND;
  expected[UC('|')][UC('|')] = CppToken::LOR;
  expected[UC('>')][UC('>')] = CppToken::RSHIFT;
  expected[UC('<')][UC('<')] = CppToken::LSHIFT;
  expected[UC('#')][UC('#')] = CppToken::DOUBLESHARP;
  expected[UC('\r')][UC('\n')] = CppToken::NEWLINE;
  expected[UC('*')][0] = CppToken::MUL;
  expected[UC('+')][0] = CppToken::ADD;
  expected[UC('-')][0] = CppToken::SUB;
  expected[UC('>')][0] = CppToken::GT;
  expected[UC('<')][0] = CppToken::LT;
  expected[UC('&')][0] = CppToken::AND;
  expected[UC('^')][0] = CppToken::XOR;
  expected[UC('|')][0] = CppToken::OR;
  expected[UC('#')][0] = CppToken::SHARP;
  expected[UC('\n')][0] = CppToken::NEWLINE;
#undef UC

  for (int i = 0; i < 128; ++i) {
    for (int j = 0; j < 128; ++j) {
      EXPECT_EQ(expected[i][j], CppTokenizer::TypeFrom(i, j));
    }
  }
}

}  // namespace devtools_goma
