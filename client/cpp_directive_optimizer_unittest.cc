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
                             &directives);

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
                             &directives);

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
                             &directives);

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
                             &directives);

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
      &directives);

  CppDirectiveOptimizer::Optimize(&directives);

  ASSERT_EQ(2U, directives.size());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[0]->type());
  EXPECT_EQ(CppDirectiveType::DIRECTIVE_IF, directives[1]->type());
}

}  // namespace devtools_goma
