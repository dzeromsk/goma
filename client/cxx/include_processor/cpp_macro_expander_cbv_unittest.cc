// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro_expander_cbv.h"

#include "cpp_tokenizer.h"
#include "gtest/gtest.h"

namespace devtools_goma {

TEST(CppMacroExpanderCBVTest, GetMacroArguments) {
  ArrayTokenList tokens;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("macro(a1, a2(b1, b2), a3, a4(c1(d))) + 1",
                                SpaceHandling::kKeep, &tokens));

  ArrayTokenList::const_iterator it = tokens.cbegin();
  CppMacroExpanderCBV::ArgRangeVector args;
  EXPECT_TRUE(CppMacroExpanderCBV::GetMacroArguments(
      tokens.cbegin(), tokens.cend(), 4, &it, &args));

  ASSERT_EQ(4U, args.size());

  // args[0] is a1
  ArrayTokenList args0(args[0].first, args[0].second);
  ArrayTokenList args0_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("a1", SpaceHandling::kKeep, &args0_expected));
  EXPECT_EQ(args0_expected, args0);

  // args[1] is a2(b1, b2)
  ArrayTokenList args1(args[1].first, args[1].second);
  ArrayTokenList args1_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a2(b1, b2)", SpaceHandling::kKeep,
                                        &args1_expected));
  EXPECT_EQ(args1_expected, args1);

  // args[2] is a3
  ArrayTokenList args2(args[2].first, args[2].second);
  ArrayTokenList args2_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("a3", SpaceHandling::kKeep, &args2_expected));
  EXPECT_EQ(args2_expected, args2);

  // args[3] is a4(c1(d))
  ArrayTokenList args3(args[3].first, args[3].second);
  ArrayTokenList args3_expected;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("a4(c1(d))", SpaceHandling::kKeep,
                                        &args3_expected));
  EXPECT_EQ(args3_expected, args3);

  // *it must be on ')'.
  EXPECT_EQ(CppToken(CppToken::PUNCTUATOR, ')'), *it);
}

TEST(CppMacroExpanderCBVTest, GetMacroArgumentsEmptyArg) {
  ArrayTokenList tokens;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("macro(a1,)", SpaceHandling::kKeep, &tokens));

  ArrayTokenList::const_iterator it = tokens.cbegin();
  CppMacroExpanderCBV::ArgRangeVector args;
  EXPECT_TRUE(CppMacroExpanderCBV::GetMacroArguments(
      tokens.cbegin(), tokens.cend(), 2, &it, &args));

  ASSERT_EQ(2U, args.size());

  // args[0] is a1
  ArrayTokenList args0(args[0].first, args[0].second);
  ArrayTokenList args0_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("a1", SpaceHandling::kKeep, &args0_expected));
  EXPECT_EQ(args0_expected, args0);

  // args[1] is empty
  ArrayTokenList args1(args[1].first, args[1].second);
  ArrayTokenList args1_expected;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("", SpaceHandling::kKeep, &args1_expected));
  EXPECT_EQ(args1_expected, args1);

  // *it must be on ')'.
  EXPECT_EQ(CppToken(CppToken::PUNCTUATOR, ')'), *it);
}

TEST(CppMacroExpanderCBVTest, GetMacroArgumentsPlentyOrShort) {
  ArrayTokenList tokens;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("macro(a1, a2(b1, b2), a3, a4(c1(d))) + 1",
                                SpaceHandling::kKeep, &tokens));

  {
    ArrayTokenList::const_iterator it = tokens.cbegin();
    CppMacroExpanderCBV::ArgRangeVector args;
    EXPECT_FALSE(CppMacroExpanderCBV::GetMacroArguments(
        tokens.cbegin(), tokens.cend(), 5, &it, &args));
  }

  {
    ArrayTokenList::const_iterator it = tokens.cbegin();
    CppMacroExpanderCBV::ArgRangeVector args;
    EXPECT_FALSE(CppMacroExpanderCBV::GetMacroArguments(
        tokens.cbegin(), tokens.cend(), 3, &it, &args));
  }
}

TEST(CppMacroExpanderCBVTest, GetMacroArgumentsEmpty) {
  ArrayTokenList tokens;
  ASSERT_TRUE(
      CppTokenizer::TokenizeAll("macro() + 1", SpaceHandling::kKeep, &tokens));

  {
    ArrayTokenList::const_iterator it = tokens.cbegin();
    CppMacroExpanderCBV::ArgRangeVector args;
    EXPECT_TRUE(CppMacroExpanderCBV::GetMacroArguments(
        tokens.cbegin(), tokens.cend(), 0, &it, &args));
    EXPECT_EQ(0U, args.size());

    // *it must be on ')'.
    EXPECT_EQ(CppToken(CppToken::PUNCTUATOR, ')'), *it);
  }

  // Use n=1 instead.
  {
    ArrayTokenList::const_iterator it = tokens.cbegin();
    CppMacroExpanderCBV::ArgRangeVector args;
    EXPECT_FALSE(CppMacroExpanderCBV::GetMacroArguments(
        tokens.cbegin(), tokens.cend(), 1, &it, &args));
  }
}

TEST(CppMacroExpanderCBVTest, GetMacroArgumentsFail) {
  {
    ArrayTokenList tokens;
    ASSERT_TRUE(
        CppTokenizer::TokenizeAll("macro(", SpaceHandling::kKeep, &tokens));

    ArrayTokenList::const_iterator it = tokens.cbegin();
    CppMacroExpanderCBV::ArgRangeVector args;
    EXPECT_FALSE(CppMacroExpanderCBV::GetMacroArguments(
        tokens.cbegin(), tokens.cend(), 2, &it, &args));
  }

  {
    ArrayTokenList tokens;
    ASSERT_TRUE(CppTokenizer::TokenizeAll("macro((1, 2), ",
                                          SpaceHandling::kKeep, &tokens));

    ArrayTokenList::const_iterator it = tokens.cbegin();
    CppMacroExpanderCBV::ArgRangeVector args;
    EXPECT_FALSE(CppMacroExpanderCBV::GetMacroArguments(
        tokens.cbegin(), tokens.cend(), 2, &it, &args));
  }

  {
    ArrayTokenList tokens;
    ASSERT_TRUE(
        CppTokenizer::TokenizeAll("macro)", SpaceHandling::kKeep, &tokens));

    ArrayTokenList::const_iterator it = tokens.cbegin();
    CppMacroExpanderCBV::ArgRangeVector args;
    EXPECT_FALSE(CppMacroExpanderCBV::GetMacroArguments(
        tokens.cbegin(), tokens.cend(), 2, &it, &args));
  }
}

}  // namespace devtools_goma
