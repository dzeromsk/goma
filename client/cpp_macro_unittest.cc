// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro.h"

#include "cpp_tokenizer.h"
#include "gtest/gtest.h"

namespace devtools_goma {

namespace {

bool IsParenBalanced(const string& s) {
  ArrayTokenList tokens;
  EXPECT_TRUE(CppTokenizer::TokenizeAll(s, true, &tokens));
  return Macro::IsParenBalanced(tokens);
}

}  // namespace

TEST(CppMacro, ParenBalanced) {
  EXPECT_TRUE(IsParenBalanced(""));
  EXPECT_TRUE(IsParenBalanced("()"));
  EXPECT_TRUE(IsParenBalanced("()()"));
  EXPECT_TRUE(IsParenBalanced("(())()"));

  EXPECT_FALSE(IsParenBalanced("("));
  EXPECT_FALSE(IsParenBalanced(")"));
  EXPECT_FALSE(IsParenBalanced(")("));
  EXPECT_FALSE(IsParenBalanced("(()"));
  EXPECT_FALSE(IsParenBalanced("())"));
  EXPECT_FALSE(IsParenBalanced("[)"));
}

TEST(CppMacro, Balanced) {
  ArrayTokenList tokens;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("()", true, &tokens));

  Macro macro("foo", Macro::OBJ, tokens, 0, false);
  EXPECT_TRUE(macro.is_paren_balanced);
}

TEST(CppMacro, Unbalanced) {
  ArrayTokenList tokens;
  ASSERT_TRUE(CppTokenizer::TokenizeAll("(", true, &tokens));

  Macro macro("foo", Macro::OBJ, tokens, 0, false);
  EXPECT_FALSE(macro.is_paren_balanced);
}

}  // namespace devtools_goma
