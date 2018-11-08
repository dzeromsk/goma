// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "token.h"

#include "gtest/gtest.h"

namespace devtools_goma {
namespace modulemap {

TEST(TokenTest, Ident) {
  Token token = Token::Ident("foo");
  EXPECT_EQ(Token::Type::IDENT, token.type());

  EXPECT_TRUE(token.IsIdent("foo"));
  EXPECT_FALSE(token.IsIdent("bar"));
  EXPECT_FALSE(token.IsPunc('*'));
  EXPECT_FALSE(token.IsPunc('!'));
  EXPECT_FALSE(token.IsInteger("123"));
  EXPECT_FALSE(token.IsInteger("321"));
  EXPECT_FALSE(token.IsString("foo"));
  EXPECT_FALSE(token.IsString("bar"));

  EXPECT_TRUE(token.IsIdentOf({"foo"}));
  EXPECT_TRUE(token.IsIdentOf({"foo", "bar"}));
  EXPECT_TRUE(token.IsIdentOf({"bar", "foo"}));

  EXPECT_FALSE(token.IsIdentOf({}));
  EXPECT_FALSE(token.IsIdentOf({"bar"}));
}

TEST(TokenTest, String) {
  Token token = Token::String("foo");
  EXPECT_EQ(Token::Type::STRING, token.type());

  EXPECT_FALSE(token.IsIdent("foo"));
  EXPECT_FALSE(token.IsIdent("bar"));
  EXPECT_FALSE(token.IsPunc('*'));
  EXPECT_FALSE(token.IsPunc('!'));
  EXPECT_FALSE(token.IsInteger("123"));
  EXPECT_FALSE(token.IsInteger("321"));
  EXPECT_TRUE(token.IsString("foo"));
  EXPECT_FALSE(token.IsString("bar"));

  EXPECT_FALSE(token.IsIdentOf({"foo", "bar"}));
  EXPECT_FALSE(token.IsIdentOf({"bar"}));
}

TEST(TokenTest, Integer) {
  Token token = Token::Integer("123");
  EXPECT_EQ(Token::Type::INTEGER, token.type());

  EXPECT_FALSE(token.IsIdent("foo"));
  EXPECT_FALSE(token.IsIdent("bar"));
  EXPECT_FALSE(token.IsPunc('*'));
  EXPECT_FALSE(token.IsPunc('!'));
  EXPECT_TRUE(token.IsInteger("123"));
  EXPECT_FALSE(token.IsInteger("321"));
  EXPECT_FALSE(token.IsString("foo"));
  EXPECT_FALSE(token.IsString("bar"));

  EXPECT_FALSE(token.IsIdentOf({"foo", "bar"}));
  EXPECT_FALSE(token.IsIdentOf({"bar"}));
}

TEST(TokenTest, Punc) {
  Token token = Token::Punc('*');
  EXPECT_EQ(Token::Type::PUNC, token.type());

  EXPECT_FALSE(token.IsIdent("foo"));
  EXPECT_FALSE(token.IsIdent("bar"));
  EXPECT_TRUE(token.IsPunc('*'));
  EXPECT_FALSE(token.IsPunc('!'));
  EXPECT_FALSE(token.IsInteger("123"));
  EXPECT_FALSE(token.IsInteger("321"));
  EXPECT_FALSE(token.IsString("foo"));
  EXPECT_FALSE(token.IsString("bar"));

  EXPECT_FALSE(token.IsIdentOf({"foo", "bar"}));
  EXPECT_FALSE(token.IsIdentOf({"bar"}));
}

TEST(TokenTest, End) {
  Token token = Token::End();
  EXPECT_EQ(Token::Type::END, token.type());

  EXPECT_FALSE(token.IsIdent("foo"));
  EXPECT_FALSE(token.IsIdent("bar"));
  EXPECT_FALSE(token.IsPunc('*'));
  EXPECT_FALSE(token.IsPunc('!'));
  EXPECT_FALSE(token.IsInteger("123"));
  EXPECT_FALSE(token.IsInteger("321"));
  EXPECT_FALSE(token.IsString("foo"));
  EXPECT_FALSE(token.IsString("bar"));

  EXPECT_FALSE(token.IsIdentOf({"foo", "bar"}));
  EXPECT_FALSE(token.IsIdentOf({"bar"}));
}

TEST(TokenTest, Invalid) {
  Token token = Token::Invalid();
  EXPECT_EQ(Token::Type::INVALID, token.type());

  EXPECT_FALSE(token.IsIdent("foo"));
  EXPECT_FALSE(token.IsIdent("bar"));
  EXPECT_FALSE(token.IsPunc('*'));
  EXPECT_FALSE(token.IsPunc('!'));
  EXPECT_FALSE(token.IsInteger("123"));
  EXPECT_FALSE(token.IsInteger("321"));
  EXPECT_FALSE(token.IsString("foo"));
  EXPECT_FALSE(token.IsString("bar"));

  EXPECT_FALSE(token.IsIdentOf({"foo", "bar"}));
  EXPECT_FALSE(token.IsIdentOf({"bar"}));
}

}  // namespace modulemap
}  // namespace devtools_goma
