// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include_guard_detector.h"

#include "cpp_directive_parser.h"
#include "gtest/gtest.h"

namespace devtools_goma {

TEST(IncludeGuardDetector, Detect) {
  // #ifndef case
  EXPECT_EQ("A_H", IncludeGuardDetector::Detect(
                       *CppDirectiveParser::ParseFromString("#ifndef A_H\n"
                                                            "#define A_H\n"
                                                            "#endif")));

  // #if !defined(X) case
  EXPECT_EQ("A_H",
            IncludeGuardDetector::Detect(
                *CppDirectiveParser::ParseFromString("#if !defined(A_H)\n"
                                                     "#define A_H\n"
                                                     "#endif")));

  // #if !defined X case
  EXPECT_EQ("A_H", IncludeGuardDetector::Detect(
                       *CppDirectiveParser::ParseFromString("#if !defined A_H\n"
                                                            "#define A_H\n"
                                                            "#endif")));

  // Add nested #if
  EXPECT_EQ("A_H", IncludeGuardDetector::Detect(
                       *CppDirectiveParser::ParseFromString("#ifndef A_H\n"
                                                            "#define A_H\n"
                                                            "#if XXX\n"
                                                            "#else\n"
                                                            "#endif\n"
                                                            "#endif")));

}

TEST(IncludeGuardDetector, DetectInvalid) {
  EXPECT_EQ("",
            IncludeGuardDetector::Detect(
                *CppDirectiveParser::ParseFromString("#if !defined(B_H) || 1\n"
                                                     "#define B_H\n"
                                                     "#endif")));

  EXPECT_EQ("",
            IncludeGuardDetector::Detect(*CppDirectiveParser::ParseFromString(

                "#if 1 || !defined(C_H)\n"
                "#define C_H\n"
                "#endif")));

  EXPECT_EQ("",
            IncludeGuardDetector::Detect(
                *CppDirectiveParser::ParseFromString("#if ID(!defined(D_H))\n"
                                                     "#define D_H\n"
                                                     "#endif")));

  // if defined
  EXPECT_EQ("", IncludeGuardDetector::Detect(
                    *CppDirectiveParser::ParseFromString("#if defined(E_H)\n"
                                                         "#define E_H\n"
                                                         "#endif")));

  // no #endif
  EXPECT_EQ("", IncludeGuardDetector::Detect(
                    *CppDirectiveParser::ParseFromString("#if !defined(F_H)\n"
                                                         "#define E_H")));

  // extra #define FOO (begin)
  EXPECT_EQ("", IncludeGuardDetector::Detect(
                    *CppDirectiveParser::ParseFromString("#define FOO\n"
                                                         "#if !defined(G_H)\n"
                                                         "#define G_H\n"
                                                         "#endif")));

  // extra #define FOO (end)
  EXPECT_EQ("",
            IncludeGuardDetector::Detect(*CppDirectiveParser::ParseFromString(
                "#if !defined(H_H)\n"
                "#define H_H\n"
                "#endif\n"
                "#define FOO\n")));

  // extra #else (end)
  EXPECT_EQ("",
            IncludeGuardDetector::Detect(*CppDirectiveParser::ParseFromString(
                "#if !defined(H_H)\n"
                "#define H_H\n"
                "#endif\n"
                "#else\n")));

  // extra #else (end)
  EXPECT_EQ("",
            IncludeGuardDetector::Detect(*CppDirectiveParser::ParseFromString(
                "#if !defined(H_H)\n"
                "#define H_H\n"
                "#endif\n"
                "#elif\n")));

  // top level else
  EXPECT_EQ("",
            IncludeGuardDetector::Detect(*CppDirectiveParser::ParseFromString(
                "#if !defined(I_H)\n"
                "#define I_H\n"
                "#else\n"
                "#define FOO\n"
                "#endif\n")));

  // top level elif
  EXPECT_EQ("",
            IncludeGuardDetector::Detect(*CppDirectiveParser::ParseFromString(
                "#if !defined(J_H)\n"
                "#define J_H\n"
                "#elif defined(X)\n"
                "#define FOO\n"
                "#endif\n")));
}

}  // namespace devtools_goma
