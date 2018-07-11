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
  EXPECT_TRUE(CppTokenizer::TokenizeAll(s, false, &tokens));

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
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a1", false, &args0_expected));
  EXPECT_EQ(args0_expected, args0);

  // args[1] is a2(b1, b2)
  ArrayTokenList args1(ToTokens(arg_ranges[1]));
  ArrayTokenList args1_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a2(b1, b2)", false, &args1_expected));
  EXPECT_EQ(args1_expected, args1);

  // args[2] is a3
  ArrayTokenList args2(ToTokens(arg_ranges[2]));
  ArrayTokenList args2_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a3", false, &args2_expected));
  EXPECT_EQ(args2_expected, args2);

  // args[3] is a4(c1(d))
  ArrayTokenList args3(ToTokens(arg_ranges[3]));
  ArrayTokenList args3_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a4(c1(d))", false, &args3_expected));
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
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a", false, &args0_expected));
  EXPECT_EQ(args0_expected, args0);

  // args[1] is empty
  ArrayTokenList args1(ToTokens(arg_ranges[1]));
  ArrayTokenList args1_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("", false, &args1_expected));
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

}  // namespace devtools_goma
