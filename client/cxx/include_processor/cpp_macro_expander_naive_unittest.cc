// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro_expander_naive.h"

#include "cpp_tokenizer.h"
#include "gtest/gtest.h"

namespace devtools_goma {

namespace {

TokenHSList TokenHSListFrom(const string& s) {
  ArrayTokenList tokens;
  EXPECT_TRUE(CppTokenizer::TokenizeAll(s, SpaceHandling::kKeep, &tokens));

  TokenHSList ths;
  for (auto&& t : tokens) {
    ths.emplace_back(std::move(t), MacroSet());
  }

  return ths;
}

ArrayTokenList ToTokens(TokenHSListRange range) {
  ArrayTokenList tokens;
  for (auto it = range.begin; it != range.end; ++it) {
    tokens.push_back(it->token);
  }
  return tokens;
}

}  // anonymous namespace

TEST(CppMacroExpanderNaiveTest, GetMacroArguments) {
  TokenHSList tokens =
      TokenHSListFrom("macro(a1, a2(b1, b2), a3, a4(c1(d))) + 1");

  std::vector<TokenHSListRange> arg_ranges;
  TokenHSList::iterator cur = tokens.begin();
  EXPECT_EQ(
      CppMacroExpanderNaive::GetMacroArgumentsResult::kOk,
      CppMacroExpanderNaive::GetMacroArguments(
          TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));

  ASSERT_EQ(4U, arg_ranges.size());

  // args[0] is a1
  ArrayTokenList args0(ToTokens(arg_ranges[0]));
  ArrayTokenList args0_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("a1", SpaceHandling::kKeep, &args0_expected));
  EXPECT_EQ(args0_expected, args0);

  // args[1] is a2(b1, b2)
  ArrayTokenList args1(ToTokens(arg_ranges[1]));
  ArrayTokenList args1_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a2(b1, b2)", SpaceHandling::kKeep,
                                        &args1_expected));
  EXPECT_EQ(args1_expected, args1);

  // args[2] is a3
  ArrayTokenList args2(ToTokens(arg_ranges[2]));
  ArrayTokenList args2_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("a3", SpaceHandling::kKeep, &args2_expected));
  EXPECT_EQ(args2_expected, args2);

  // args[3] is a4(c1(d))
  ArrayTokenList args3(ToTokens(arg_ranges[3]));
  ArrayTokenList args3_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a4(c1(d))", SpaceHandling::kKeep,
                                        &args3_expected));
  EXPECT_EQ(args3_expected, args3);

  // *cur must be on ')'.
  EXPECT_EQ(CppToken(CppToken::PUNCTUATOR, ')'), cur->token);
}

TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsEmpty) {
  TokenHSList tokens = TokenHSListFrom("macro()");

  std::vector<TokenHSListRange> arg_ranges;
  TokenHSList::iterator cur = tokens.begin();
  EXPECT_EQ(
      CppMacroExpanderNaive::GetMacroArgumentsResult::kOk,
      CppMacroExpanderNaive::GetMacroArguments(
          TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));

  EXPECT_EQ(0U, arg_ranges.size());

  // *cur must be on ')'.
  EXPECT_EQ(CppToken(CppToken::PUNCTUATOR, ')'), cur->token);
}

TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsEmptyArg) {
  TokenHSList tokens = TokenHSListFrom("macro(a,)");

  std::vector<TokenHSListRange> arg_ranges;
  TokenHSList::iterator cur = tokens.begin();
  EXPECT_EQ(
      CppMacroExpanderNaive::GetMacroArgumentsResult::kOk,
      CppMacroExpanderNaive::GetMacroArguments(
          TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));

  ASSERT_EQ(2U, arg_ranges.size());

  // args[0] is a
  ArrayTokenList args0(ToTokens(arg_ranges[0]));
  ArrayTokenList args0_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("a", SpaceHandling::kKeep, &args0_expected));
  EXPECT_EQ(args0_expected, args0);

  // args[1] is empty
  ArrayTokenList args1(ToTokens(arg_ranges[1]));
  ArrayTokenList args1_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("", SpaceHandling::kKeep, &args1_expected));
  EXPECT_EQ(args1_expected, args1);

  // *cur must be on ')'.
  EXPECT_EQ(CppToken(CppToken::PUNCTUATOR, ')'), cur->token);
}

TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsWithSpaces) {
  // [FOO][(][ ][1][ ][,][ ][F][ ][(][ ][1][ ][,][ ][2][ ][)][ ][)][ ][X]
  //    0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21

  TokenHSList tokens = TokenHSListFrom("FOO( 1 , F ( 1 , 2 ) ) X");
  ASSERT_EQ(22, tokens.size());

  std::vector<TokenHSListRange> arg_ranges;
  TokenHSList::iterator cur = tokens.begin();
  EXPECT_EQ(
      CppMacroExpanderNaive::GetMacroArgumentsResult::kOk,
      CppMacroExpanderNaive::GetMacroArguments(
          TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));

  ASSERT_EQ(2U, arg_ranges.size());

  // args[0] is "1"
  ArrayTokenList args0(ToTokens(arg_ranges[0]));
  ArrayTokenList args0_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("1", SpaceHandling::kKeep, &args0_expected));
  EXPECT_EQ(args0_expected, args0);

  // args[1] is "F(1, 2)"
  ArrayTokenList args1(ToTokens(arg_ranges[1]));
  ArrayTokenList args1_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("F ( 1 , 2 )", SpaceHandling::kKeep,
                                        &args1_expected));
  EXPECT_EQ(args1_expected, args1);

  // *cur must be on ')'.
  EXPECT_EQ(CppToken(CppToken::PUNCTUATOR, ')'), cur->token);
}

TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsNoParen) {
  {
    TokenHSList tokens = TokenHSListFrom("macro");

    std::vector<TokenHSListRange> arg_ranges;
    TokenHSList::iterator cur = tokens.begin();
    EXPECT_EQ(
        CppMacroExpanderNaive::GetMacroArgumentsResult::kNoParen,
        CppMacroExpanderNaive::GetMacroArguments(
            TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));
  }

  {
    TokenHSList tokens = TokenHSListFrom("macro A B C");

    std::vector<TokenHSListRange> arg_ranges;
    TokenHSList::iterator cur = tokens.begin();
    EXPECT_EQ(
        CppMacroExpanderNaive::GetMacroArgumentsResult::kNoParen,
        CppMacroExpanderNaive::GetMacroArguments(
            TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));
  }

  {
    TokenHSList tokens = TokenHSListFrom("macro)");

    std::vector<TokenHSListRange> arg_ranges;
    TokenHSList::iterator cur = tokens.begin();
    EXPECT_EQ(
        CppMacroExpanderNaive::GetMacroArgumentsResult::kNoParen,
        CppMacroExpanderNaive::GetMacroArguments(
            TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));
  }
}

TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsUnterminatedParen) {
  {
    TokenHSList tokens = TokenHSListFrom("macro(");

    std::vector<TokenHSListRange> arg_ranges;
    TokenHSList::iterator cur = tokens.begin();
    EXPECT_EQ(
        CppMacroExpanderNaive::GetMacroArgumentsResult::kUnterminatedParen,
        CppMacroExpanderNaive::GetMacroArguments(
            TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));
  }

  {
    TokenHSList tokens = TokenHSListFrom("macro((1, 2), ");

    std::vector<TokenHSListRange> arg_ranges;
    TokenHSList::iterator cur = tokens.begin();
    EXPECT_EQ(
        CppMacroExpanderNaive::GetMacroArgumentsResult::kUnterminatedParen,
        CppMacroExpanderNaive::GetMacroArguments(
            TokenHSListRange(tokens.begin(), tokens.end()), &cur, &arg_ranges));
  }
}

TEST(CppMacroExpanderNaiveTest, GetVaOptArgument) {
  const string s = "( 1, F(1, 2) ) X";
  ArrayTokenList tokens;
  ASSERT_TRUE(CppTokenizer::TokenizeAll(s, SpaceHandling::kKeep, &tokens));

  // [(][ ][1][,][ ][F][(][1][,][ ][2][)][ ][)][ ][X]
  //  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15

  ASSERT_EQ(16, tokens.size());

  ArrayTokenList::const_iterator argument_begin;
  ArrayTokenList::const_iterator argument_end;
  ArrayTokenList::const_iterator right_paren_pos;
  EXPECT_TRUE(CppMacroExpanderNaive::GetVaOptArgument(
      tokens.begin(), tokens.end(), &argument_begin, &argument_end,
      &right_paren_pos));
  EXPECT_EQ(2, argument_begin - tokens.begin());
  // The trailing space is omitted, so argument_end is 12 instead of 13.
  EXPECT_EQ(12, argument_end - tokens.begin());
  EXPECT_EQ(13, right_paren_pos - tokens.begin());
}

}  // namespace devtools_goma
