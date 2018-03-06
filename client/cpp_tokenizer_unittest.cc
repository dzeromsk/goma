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

  CppInputStream stream(std::move(c), FileId(), "");
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

  EXPECT_FALSE(ReadCharLiteral("''", &token, !kCHECK_END));

  EXPECT_FALSE(ReadCharLiteral("'", &token, !kCHECK_END));

  EXPECT_FALSE(ReadCharLiteral("'\\", &token, !kCHECK_END));

  EXPECT_FALSE(ReadCharLiteral("'0", &token, !kCHECK_END));
}

}  // namespace devtools_goma
