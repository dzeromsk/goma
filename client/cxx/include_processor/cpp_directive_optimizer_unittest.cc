// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_directive_optimizer.h"

#include "cpp_directive_parser.h"
#include "gtest/gtest.h"

namespace devtools_goma {

TEST(CppDirectiveOptimizerTest, IfdefEndif) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(*Content::CreateFromString("#ifdef X\n"
                                                        "#endif\n"
                                                        "#define A\n"
                                                        "#ifndef X\n"
                                                        "#endif\n"
                                                        "#define B\n"),
                             "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  ASSERT_EQ(2U, directives.size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, directives[1]->type());
}

TEST(CppDirectiveOptimizerTest, IfEndif) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(*Content::CreateFromString("#if X\n"
                                                        "#endif\n"
                                                        "#define A\n"
                                                        "#if X\n"
                                                        "#elif X\n"
                                                        "#elif X\n"
                                                        "#endif\n"
                                                        "#define B\n"),
                             "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  ASSERT_EQ(2U, directives.size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, directives[1]->type());
}

TEST(CppDirectiveOptimizerTest, IfElifEndif) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(*Content::CreateFromString("#if X\n"
                                                        "#define X\n"
                                                        "#endif\n"
                                                        "#if X\n"
                                                        "#define Y\n"
                                                        "#elif X\n"
                                                        "#elif X\n"
                                                        "#endif\n"),
                             "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  // Last two elif can be removed.

  ASSERT_EQ(6U, directives.size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, directives[1]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[2]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[3]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_DEFINE, directives[4]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[5]->type());
}

TEST(CppDirectiveOptimizerTest, IfDefIfndef) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(*Content::CreateFromString("#if defined(X)\n"
                                                        "#if defined (X)\n"
                                                        "#if defined X\n"
                                                        "#if !defined(Y)\n"
                                                        "#if !defined (Y)\n"
                                                        "#if !defined X\n"),
                             "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  ASSERT_EQ(6U, directives.size());
  // #if defined(X) is converted to #ifdef X
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFDEF, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFDEF, directives[1]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFDEF, directives[2]->type());
  // #if !defined(Y) is converted to #ifndef Y
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFNDEF, directives[3]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFNDEF, directives[4]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IFNDEF, directives[5]->type());
}

TEST(CppDirectiveOptimizerTest, IfDefIfndefUnconverted) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(
      *Content::CreateFromString("#if defined(X) || defined(Y)\n"
                                 "#if !defined(X) && !defined(Y)\n"),
      "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  ASSERT_EQ(2U, directives.size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[1]->type());
}

// If #if contains __has_include() or __has_include_next(), we should keep it.
TEST(CppDirectiveOptimizerTest, IfHasInclude) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(
      *Content::CreateFromString("#if __has_include(X)\n"
                                 "#endif\n"
                                 "#if FOO && __has_include(X)\n"
                                 "#endif\n"
                                 "#if __has_include(X) || FOO\n"
                                 "#endif\n"
                                 "#if __has_include_next(Y)\n"
                                 "#endif\n"
                                 "#if FOO && __has_include_next(Y)\n"
                                 "#endif\n"
                                 "#if __has_include_next(Y) || FOO\n"
                                 "#endif\n"),
      "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  ASSERT_EQ(12U, directives.size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[1]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[2]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[3]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[4]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[5]->type());

  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[6]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[7]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[8]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[9]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[10]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[11]->type());
}

// If #if contains __has_include() or __has_include_next(), we should keep it.
TEST(CppDirectiveOptimizerTest, ElifHasInclude) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(
      *Content::CreateFromString("#if X\n"
                                 "#elif __has_include(X)\n"
                                 "#endif\n"
                                 "#if Y\n"
                                 "#elif __has_include_next(Y)\n"
                                 "#endif\n"),
      "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  ASSERT_EQ(6U, directives.size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ELIF, directives[1]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[2]->type());

  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[3]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ELIF, directives[4]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_ENDIF, directives[5]->type());
}

TEST(CppDirectiveOptimizerTest, Complex1) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(
      *Content::CreateFromString("#if 1\n"
                                 "# pragma once\n"
                                 "#else\n"
                                 "# error removed_error\n"
                                 "#endif\n"
                                 "#ifdef x\n"
                                 "# pragma comment(lib, \"hoge\")\n"
                                 "#endif\n"
                                 "#ifndef x\n"
                                 "#elif 2\n"
                                 "#else\n"
                                 "#endif\n"),
      "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  CppDirectiveList expected;
  CppDirectiveParser().Parse(*Content::CreateFromString("#if 1\n"
                                                        "#pragma once\n"
                                                        "#endif\n"),
                             "<string>", &expected);

  ASSERT_EQ(expected.size(), directives.size());
  for (size_t i = 0; i < directives.size(); ++i) {
    EXPECT_EQ(expected[i]->type(), directives[i]->type());
  }
}

TEST(CppDirectiveOptimizerTest, Complex2) {
  CppDirectiveList directives;
  CppDirectiveParser().Parse(
      *Content::CreateFromString("#if X\n"
                                 "# if Y\n"
                                 "# endif\n"
                                 "#endif\n"
                                 "#define hoge\n"
                                 "#if X\n"
                                 "# if Y\n"
                                 "#  include \"something.h\"\n"
                                 "# endif\n"
                                 "#endif\n"
                                 "#define fuga\n"
                                 "#if X\n"
                                 "# if Y\n"
                                 "# elif Z\n"
                                 "#  include \"something.h\"\n"
                                 "# endif\n"
                                 "#endif\n"),
      "<string>", &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  CppDirectiveList expected;
  CppDirectiveParser().Parse(
      *Content::CreateFromString("#define hoge\n"
                                 "#if X\n"
                                 "#if Y\n"
                                 "#include \"something.h\"\n"
                                 "#endif\n"
                                 "#endif\n"
                                 "#define fuga\n"
                                 "#if X\n"
                                 "#if Y\n"
                                 "#elif Z\n"
                                 "#include \"something.h\"\n"
                                 "#endif\n"
                                 "#endif\n"),
      "<string>", &expected);

  ASSERT_EQ(expected.size(), directives.size());
  for (size_t i = 0; i < directives.size(); ++i) {
    EXPECT_EQ(expected[i]->type(), directives[i]->type());
  }
}

}  // namespace devtools_goma
