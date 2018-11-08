// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lexer.h"

#include "glog/stl_logging.h"
#include "gtest/gtest.h"

namespace devtools_goma {
namespace modulemap {

TEST(LexerTest, Empty) {
  std::unique_ptr<Content> content = Content::CreateFromString("");

  std::vector<Token> tokens;
  EXPECT_TRUE(Lexer::Run(*content, &tokens));

  ASSERT_EQ(0U, tokens.size());
}

TEST(LexerTest, Basic) {
  std::unique_ptr<Content> content =
      Content::CreateFromString("foo [bar] 12 \"foo\" bar_123 _");

  // foo [ bar ] 12 "foo" bar_123 _
  std::vector<Token> tokens;
  EXPECT_TRUE(Lexer::Run(*content, &tokens));

  ASSERT_EQ(8U, tokens.size());
  EXPECT_TRUE(tokens[0].IsIdent("foo")) << tokens[0];
  EXPECT_TRUE(tokens[1].IsPunc('[')) << tokens[1];
  EXPECT_TRUE(tokens[2].IsIdent("bar")) << tokens[2];
  EXPECT_TRUE(tokens[3].IsPunc(']')) << tokens[3];
  EXPECT_TRUE(tokens[4].IsInteger("12")) << tokens[4];
  EXPECT_TRUE(tokens[5].IsString("foo")) << tokens[5];
  EXPECT_TRUE(tokens[6].IsIdent("bar_123")) << tokens[6];
  EXPECT_TRUE(tokens[7].IsIdent("_")) << tokens[7];
}

TEST(LexerTest, IntegerWithSuffix) {
  std::unique_ptr<Content> content = Content::CreateFromString("123bar");

  // TODO: Currently we parse this as [123][bar].
  // Should we make this invalid?
  std::vector<Token> tokens;
  EXPECT_TRUE(Lexer::Run(*content, &tokens));

  ASSERT_EQ(2U, tokens.size());
  EXPECT_TRUE(tokens[0].IsInteger("123")) << tokens[0];
  EXPECT_TRUE(tokens[1].IsIdent("bar")) << tokens[1];
}

TEST(LexerTest, StringNotClosed) {
  std::unique_ptr<Content> content = Content::CreateFromString("\"123bar");

  std::vector<Token> tokens;
  EXPECT_FALSE(Lexer::Run(*content, &tokens));
}

TEST(LexerTest, SkipComment) {
  std::unique_ptr<Content> content = Content::CreateFromString(R"(
// a one line comment
1
/* block comment */
2
/* block comment
   multiple lines
   // and dummy one line comment here
*/
3
// /* one line comment
4
/*/ evil case of block comment /*/
5
/* /* nested block comment, but the latter '*' '/' is not comment actually.
 */ */
)");

  std::vector<Token> tokens;
  EXPECT_TRUE(Lexer::Run(*content, &tokens));

  ASSERT_EQ(7U, tokens.size()) << tokens;
  EXPECT_TRUE(tokens[0].IsInteger("1")) << tokens[0];
  EXPECT_TRUE(tokens[1].IsInteger("2")) << tokens[1];
  EXPECT_TRUE(tokens[2].IsInteger("3")) << tokens[2];
  EXPECT_TRUE(tokens[3].IsInteger("4")) << tokens[3];
  EXPECT_TRUE(tokens[4].IsInteger("5")) << tokens[4];
  EXPECT_TRUE(tokens[5].IsPunc('*')) << tokens[5];
  EXPECT_TRUE(tokens[6].IsPunc('/')) << tokens[6];
}

TEST(LexerTest, CommentWithoutNewLine) {
  std::unique_ptr<Content> content = Content::CreateFromString(R"(
1
// comment without NL )");

  std::vector<Token> tokens;
  EXPECT_TRUE(Lexer::Run(*content, &tokens));

  ASSERT_EQ(1U, tokens.size()) << tokens;
  EXPECT_TRUE(tokens[0].IsInteger("1")) << tokens[0];
}

TEST(LexerTest, CommentNotEnded) {
  // Implementation Note: I intentionally separate "/" and "*", otherwise
  // cpplint warns. I couldn't make NOLINT work for this warning.
  std::unique_ptr<Content> content = Content::CreateFromString(
      "/"
      "*");

  std::vector<Token> tokens;
  EXPECT_FALSE(Lexer::Run(*content, &tokens));
}

}  // namespace modulemap
}  // namespace devtools_goma
