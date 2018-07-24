// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro_expander.h"

#include "content.h"
#include "cpp_macro_expander_cbv.h"
#include "cpp_macro_expander_naive.h"
#include "cpp_parser.h"
#include "cpp_tokenizer.h"
#include "gtest/gtest.h"

namespace devtools_goma {

// For all correct testcases,
//   1. CBV expander should pass or fail.
//   2. Naive expander should pass.
// For all errorous testcaess,
//   1. CBV expander should fail.
//   2. Naive expander should fail.

enum class CheckFlag {
  // Both CBV and naive expander should pass.
  kPassAll,
  // CBV expander should fail, naive expander should pass.
  kPassNaive,
  // Both CBV and naive expander should fail.
  kError,
};

class CppErrorObserver : public CppParser::ErrorObserver {
 public:
  CppErrorObserver() = default;
  ~CppErrorObserver() override = default;

  CppErrorObserver(const CppErrorObserver&) = delete;
  void operator=(const CppErrorObserver&) = delete;

  void HandleError(const string& error) override {
    errors_.push_back(error);
  }
  const std::vector<string>& errors() const {
    return errors_;
  }

  void ClearError() {
    errors_.clear();
  }

 private:
  std::vector<string> errors_;
};

void CheckExpand(CheckFlag check_flag,
                 const string& defines,
                 const string& expand,
                 const string& expected) {
  CppErrorObserver error_observer;
  CppParser cpp_parser;
  cpp_parser.set_error_observer(&error_observer);
  cpp_parser.AddStringInput(defines, "(string)");
  EXPECT_TRUE(cpp_parser.ProcessDirectives());

  ArrayTokenList tokens;
  ASSERT_TRUE(CppTokenizer::TokenizeAll(expand, false, &tokens));

  // For expected tokens, remove spaces since ExpandMacro() has also
  // skip_space=true.
  ArrayTokenList expected_tokens;
  ASSERT_TRUE(CppTokenizer::TokenizeAll(expected, true, &expected_tokens));

  {
    ArrayTokenList expanded;
    CppMacroExpanderNaive(&cpp_parser).ExpandMacro(tokens, true, &expanded);
    if (check_flag == CheckFlag::kError) {
      EXPECT_FALSE(error_observer.errors().empty())
          << "should fail, but succeeded\n"
          << "defines: " << defines << '\n'
          << "expand: " << expand << '\n'
          << "expected: " << expected << '\n'
          << "expanded: " << DebugString(expanded);
    } else {
      EXPECT_EQ(expected_tokens, expanded)
          << "failed for naive case\n"
          << "defines: " << defines << '\n'
          << "expand: " << expand << '\n'
          << "expected: " << expected << '\n'
          << "expanded: " << DebugString(expanded);
    }
  }

  // This expander should expand the macro correctly or just fail.
  {
    error_observer.ClearError();
    ArrayTokenList expanded;
    bool ok =
        CppMacroExpanderCBV(&cpp_parser).ExpandMacro(tokens, true, &expanded);
    if (check_flag == CheckFlag::kPassAll) {
      EXPECT_TRUE(ok);
      EXPECT_EQ(expected_tokens, expanded)
          << "failed for CBV case\n"
          << "defines: " << defines << '\n'
          << "expand: " << expand << '\n'
          << "expected: " << expected << '\n'
          << "expanded: " << DebugString(expanded);
    } else {
      EXPECT_FALSE(ok) << "unexpectedly ok\n"
                       << "defines: " << defines << '\n'
                       << "expand: " << expand << '\n'
                       << "expected: " << expected << '\n'
                       << "expanded: " << DebugString(expanded);
    }
  }
}

TEST(CppMacroExpanderTest, ExpandEmpty) {
  CheckExpand(CheckFlag::kPassAll,
              "",
              "",
              "");
}

TEST(CppMacroExpanderTest, ExpandObjectLikeMacro) {
  CheckExpand(CheckFlag::kPassAll,
              "#define A B",
              "A",
              "B");

  CheckExpand(CheckFlag::kPassAll,
              "#define A 1",
              "A",
              "1");

  CheckExpand(CheckFlag::kPassAll,
              "#define A 1",
              "A()",
              "1()");

  CheckExpand(CheckFlag::kPassAll,
              "#define A B\n"
              "#define B 1\n",
              "A",
              "1");
}

TEST(CppMacroExpanderTest, ExpandFunctionLikeMacro) {
  CheckExpand(CheckFlag::kPassAll,
              "#define F(X) X\n",
              "F(1+2)",
              "1+2");

  CheckExpand(CheckFlag::kPassAll,
              "#define F(X, Y) X + Y\n",
              "F(1, 2)",
              "1 + 2");

  // For F(X), F() is allowed. (X = "")
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X) X 1\n",
              "F()",
              "1");

  // For F(X), F() is allowed. (X = "")
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X) G(X, 1)\n",
              "F()",
              "G(, 1)");

  // this pattern looks normal, but G is not defined, so comma appears
  // in non argument pattern. So CBV expander won't handle this.
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X) G(10, X)\n",
              "F(1)",
              "G(10, 1)");
}

// CBV expander does not handle variadic macros.
TEST(CppMacroExpanderTest, ExpandFunctionLikeMacroVariadic) {
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) X + __VA_ARGS__",
              "F()",
              "+");

  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) X + __VA_ARGS__",
              "F(1)",
              "1 +");

  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) X + __VA_ARGS__",
              "F(1, 2)",
              "1 + 2");

  CheckExpand(CheckFlag::kPassNaive,
              "#define f(...) __VA_ARGS__",
              "f()",
              "");

  CheckExpand(CheckFlag::kPassNaive,
              "#define f(...) __VA_ARGS__",
              "f(x)",
              "x");

  CheckExpand(CheckFlag::kPassNaive,
              "#define f(...) __VA_ARGS__",
              "f(x,y)",
              "x,y");

  CheckExpand(CheckFlag::kPassNaive,
              "#define f(...) __VA_ARGS__\n"
              "#define x 1\n"
              "#define y 2\n",
              "f(x,y)",
              "1,2");

  CheckExpand(CheckFlag::kPassNaive,
              "#define f(x, y, ...) __VA_ARGS__, y, x\n",
              "f(1, 2)",
              ",2,1");

  CheckExpand(CheckFlag::kPassNaive,
              "#define f(x, y, ...) __VA_ARGS__, y, x\n",
              "f(1, 2, 3, 4)",
              "3,4,2,1");

  CheckExpand(CheckFlag::kPassNaive,
              "#define X(a, b, c, ...) c\n",
              "X(\"a\", \"b\", \"c\", \"d\", \"e\")",
              "\"c\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define two(...) __VA_ARGS__, __VA_ARGS__\n",
              "two(two(1), two(2))",
              "1,1,2,2,1,1,2,2");
}

TEST(CppMacroExpanderTest, ExpandVaOpt) {
  CheckExpand(CheckFlag::kPassNaive,
              "#define f(a, ...) g(a __VA_OPT__(,) __VA_ARGS__)\n", "f(1)",
              "g(1)");
  CheckExpand(CheckFlag::kPassNaive,
              "#define f(a, ...) g(a __VA_OPT__(,) __VA_ARGS__)\n", "f(1, 2)",
              "g(1, 2)");

  CheckExpand(CheckFlag::kPassNaive,
              "#define f(a, ...) g(a __VA_OPT__(# __VA_ARGS__))\n", "f(1, 2)",
              "g(1 \"2\")");

  // F(1, 2) --> G(1, 100, 200) --> X=1 Y=100 Z=200
  CheckExpand(CheckFlag::kPassNaive,
              "#define G(x, y, z) X=x Y=y Z=z\n"
              "#define F(x, ...) G(1, __VA_OPT__(100, 200))\n",
              "F(1, 2)", "X=1 Y=100 Z=200");

  // F(1, 2) --> G(1, H(100, 200)) --> argument number mismatch
  // H is not expanded here.
  CheckExpand(CheckFlag::kError,
              "#define H(x, y) x, y\n"
              "#define G(x, y, z) X=x Y=y Z=z\n"
              "#define F(x, ...) G(1, __VA_OPT__(H(100, 200)))\n",
              "F(1, 2)", "");

  CheckExpand(CheckFlag::kPassNaive, "#define F(...) #__VA_OPT__(G(1, 2)) X\n",
              "F()", "\"\" X");

  CheckExpand(CheckFlag::kPassNaive, "#define F(...) #__VA_OPT__(G(1, 2)) X\n",
              "F(1)", "\"G(1, 2)\" X");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(...) #__VA_OPT__  (  G(1, 2)  ) X\n", "F(1)",
              "\"G(1, 2)\" X");

  // error: '#' is not followed by a macro parameter
  CheckExpand(CheckFlag::kError, "#define f(a, b, ...) g(a __VA_OPT__(#) b)\n",
              "f(1, 2, 3)", "");
  CheckExpand(CheckFlag::kError,
              "#define f(a, b, ...) g(a __VA_OPT__(  #  ) b)\n", "f(1, 2, 3)",
              "");
  CheckExpand(CheckFlag::kError,
              "#define f(a, b, ...) g(a __VA_OPT__  (  #  ) b)\n", "f(1, 2, 3)",
              "");

  // __VA_OPT__ with ##
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, ...) A ## __VA_OPT__(__VA_ARGS__) B", "F()", "B");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, ...) A ## __VA_OPT__(__VA_ARGS__) B", "F(a)",
              "a B");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, ...) A ## __VA_OPT__(__VA_ARGS__) B", "F(a, b)",
              "ab B");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, ...) A ## __VA_OPT__(__VA_ARGS__) B", "F(a, b, c)",
              "ab, c B");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, ...) A ## __VA_OPT__  (  __VA_ARGS__  ) B",
              "F(a, b, c)", "ab, c B");

  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, B, ...) A ## __VA_OPT__(B ## __VA_ARGS__) B",
              "F(a, b)", "a b");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, B, ...) A ## __VA_OPT__(B ## __VA_ARGS__) B",
              "F(a, b, c)", "abc b");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, B, ...) A ## __VA_OPT__  (  B ## __VA_ARGS__  ) B",
              "F(a, b, c)", "abc b");

  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, B, ...) __VA_OPT__(B ## __VA_ARGS__) ## A",
              "F(a, b)", "a");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, B, ...) __VA_OPT__(B ## __VA_ARGS__) ## A",
              "F(a, b, c)", "bca");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(A, B, ...) __VA_OPT__  (  B ## __VA_ARGS__  ) ## A",
              "F(a, b, c)", "bca");

  // paren is missing
  CheckExpand(CheckFlag::kError, "#define f(a, b, ...) __VA_OPT__\n",
              "f(1, 2, 3)", "");

  CheckExpand(CheckFlag::kError, "#define f(a, b, ...) __VA_OPT__(\n",
              "f(1, 2, 3)", "");
  CheckExpand(CheckFlag::kError, "#define f(a, b, ...) __VA_OPT__(()\n",
              "f(1, 2, 3)", "");

  // __VA_OPT__ in no variadic function. It continues with warning.
  CheckExpand(CheckFlag::kPassNaive, "#define f(a, b) __VA_OPT__(foo) a b",
              "f(1, 2)", "1 2");
  // Interestingly, clang prevers __VA_OPT__ if argument size is 0.
  // (In this case, CBV version passes since __VA_OPT__ is not considered as a
  // special form)
  CheckExpand(CheckFlag::kPassAll, "#define f() __VA_OPT__(foo) a b", "f()",
              "__VA_OPT__(foo) a b");
  // __VA_OPT__ in no variadic function. It continues with warning.
  // __VA_OPT__ is preserved.
  CheckExpand(CheckFlag::kPassAll, "#define f __VA_OPT__(foo) a b", "f",
              "__VA_OPT__(foo) a b");
}

// These expander should fail due to argument number mismatch
TEST(CppMacroExpanderTest, ExpandFunctionLikeMacroError) {
  CheckExpand(CheckFlag::kError,
              "#define F(X, Y) X + Y",
              "F(1)",
              "");

  CheckExpand(CheckFlag::kError,
              "#define F(X, Y) X + Y",
              "F(1, 2, 3)",
              "");

  CheckExpand(CheckFlag::kError,
              "#define F(X, Y, ...) X + Y + __VA_ARGS__",
              "F()",
              "");

  CheckExpand(CheckFlag::kError,
              "#define F(X, Y, ...) X + Y + __VA_ARGS__",
              "F(1)",
              "");
}

TEST(CppMacroExpanderTest, ExpandHideSet) {
  CheckExpand(CheckFlag::kPassAll,
              "#define A A\n",
              "A",
              "A");

  CheckExpand(CheckFlag::kPassAll,
              "#define A B\n"
              "#define B C\n"
              "#define C A\n",
              "A",
              "A");

  CheckExpand(CheckFlag::kPassAll,
              "#define F(X) G(X)\n"
              "#define G(X) F(X) + 1",
              "F(1)",
              "F(1) + 1");
}

// This test does not pass with CBV expander.
// CBV expander does not handle stringize.
TEST(CppMacroExpanderTest, Stringize) {
  CheckExpand(CheckFlag::kPassNaive,
              "#define STRINGIFY(x) #x",
              "STRINGIFY(a)",
              "\"a\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define STRINGIFY(x) # x",
              "STRINGIFY(a)",
              "\"a\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(...) # __VA_ARGS__",
              "A()",
              "\"\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(...) # __VA_ARGS__",
              "A(1)",
              "\"1\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(...) # __VA_ARGS__",
              "A(1, 2)",
              "\"1, 2\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(...) # __VA_ARGS__",
              "A(1, 2, 3)",
              "\"1, 2, 3\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define STR1(x) #x\n"
              "#define THE_ANSWER 42\n"
              "#define THE_ANSWER_STR STR1(THE_ANSWER)\n",
              "THE_ANSWER_STR",
              "\"THE_ANSWER\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define STR1(x) #x\n"
              "#define STR2(x) STR1(x)\n"
              "#define THE_ANSWER 42\n"
              "#define THE_ANSWER_STR STR2(THE_ANSWER)\n",
              "THE_ANSWER_STR",
              "\"42\"");
}

// This test does not pass with CBV expander.
// CBV expander does not handle glue.
TEST(CppMacroExpanderTest, Glue) {
  CheckExpand(CheckFlag::kPassNaive,
              "#define A B ## C\n",
              "A",
              "BC");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A() B ## C\n",
              "A()",
              "BC");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(X) B ## X\n",
              "A(C)",
              "BC");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(X) B ## X\n",
              "A(C+1)",
              "BC+1");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(X) X ## C\n",
              "A(B)",
              "BC");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(X) X ## C\n",
              "A(1+B)",
              "1+BC");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(X, Y) X ## Y",
              "A(B, C)",
              "BC");

  CheckExpand(CheckFlag::kPassNaive,
              "#define A(X, Y) X ## Y",
              "A(B+C, D+E)",
              "B+CD+E");

  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) X ## __VA_ARGS__",
              "F()",
              "");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) X ## __VA_ARGS__",
              "F(A)",
              "A");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) X ## __VA_ARGS__",
              "F(A, B)",
              "AB");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) X ## __VA_ARGS__",
              "F(A, B, C)",
              "AB, C");

  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) __VA_ARGS__ ## X",
              "F()",
              "");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) __VA_ARGS__ ## X",
              "F(A)",
              "A");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) __VA_ARGS__ ## X",
              "F(A, B)",
              "BA");
  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X, ...) __VA_ARGS__ ## X",
              "F(A, B, C)",
              "B, CA");

  CheckExpand(CheckFlag::kPassNaive,
              "#define CAT(x, y) x ## y\n"
              "#define FOO CAT(1+2,2+3)\n",
              "FOO",
              "1+22+3");

  CheckExpand(CheckFlag::kPassNaive,
              "#define CAT1(x, y) x ## y\n"
              "#define CAT(x, y) CAT1(x, y)\n"
              "#define F(X) X\n"
              "#define G(X) X\n"
              "#define FOO CAT(F(1), G(2))\n",
              "FOO",
              "12");

  // Regression test from b/78436008
  CheckExpand(CheckFlag::kPassNaive,
              "#define _WIN32_WINNT 0x0600\n"
              "#define NV_FROM_WIN32_WINNT2(V) V##0000\n"
              "#define NV_FROM_WIN32_WINNT(V) NV_FROM_WIN32_WINNT2(V)\n"
              "#define NV NV_FROM_WIN32_WINNT(_WIN32_WINNT)\n",
              "NV",
              "0x06000000");

  CheckExpand(CheckFlag::kPassNaive,
              "#define GLUE(X, Y) X ## Y\n",
              "GLUE(\"foo\", )",
              "\"foo\"");

  CheckExpand(CheckFlag::kPassNaive,
              "#define GLUE(X, Y) X ## Y\n",
              "GLUE(, \"foo\")",
              "\"foo\"");

  CheckExpand(CheckFlag::kError,
              "#define GLUE(X, Y) X ## Y\n",
              "GLUE(\"foo\", \"bar\")",
              "");

  CheckExpand(CheckFlag::kPassNaive,
              "#define GLUE(X, Y) X ## Y\n",
              "GLUE(|, |)",
              "||");
}

TEST(CppMacroExpanderTest, Complex) {
  CheckExpand(CheckFlag::kPassAll,
              "#define f(x) f\n"
              "#define foo f(x)(y)\n",
              "foo",
              "f(y)");

  CheckExpand(CheckFlag::kPassAll,
              "#define id(x) x\n",
              "id(id(a))",
              "a");

  CheckExpand(CheckFlag::kPassAll,
              "#define a",
              "a",
              "");

  CheckExpand(CheckFlag::kPassAll,
              "",
              "a",
              "a");

  CheckExpand(CheckFlag::kPassAll,
              "#define f",
              "f(x)",
              "(x)");

  // CBV does not handle this, since f cannot be evaluated into
  // non-expandable tokens (f is a macro).
  CheckExpand(CheckFlag::kPassNaive,
              "#define f(x)",
              "f",
              "f");

  // If we allow calling a function with argument length mismatch,
  // this case cannot be detected. This case must fail on CBV expander.
  CheckExpand(CheckFlag::kPassNaive,
              "#define X 1,2\n"
              "#define ADD1(x, y) x+y\n"
              "#define ADD2(X) ADD1(X)\n"
              "#define FOO ADD2(X)\n",
              "FOO",
              "1+2");

  // Unbalanced parens.
  // CBV expander should fail for this test.
  CheckExpand(CheckFlag::kPassNaive,
              "#define BOO() 123\n"
              "#define FOO(y) BOO y )\n"
              "#define OPEN (\n",
              "FOO(OPEN)",
              "123");

  // Unbalanced parens.
  // CBV expander should fail for this test.
  CheckExpand(CheckFlag::kPassNaive,
              "#define A )\n"
              "#define B (\n"
              "#define C() 1\n"
              "#define FOO C B A\n"
              "#define ID(X) X\n",
              "ID(FOO)",
              "1");

  // Unbalanced parens. should be argument mismatch.
  CheckExpand(CheckFlag::kError,
              "#define A (\n"
              "#define G(X, Y) X + Y\n"
              "#define F(X, Y) G(X, Y)\n",
              "F(A, 1)",
              "G((,1)");

  // Comma appears. should be argument number mismatch.
  CheckExpand(CheckFlag::kError,
              "#define A 1, 2\n"
              "#define G(X, Y) X + Y\n"
              "#define F(X, Y) G(X, Y)\n"
              "#define ID(X) X\n",
              "ID(F(A, 1))",
              "");

  // Regression test: This was failing before.
  CheckExpand(CheckFlag::kPassAll,
             "#define e(x) ee(x)\n"
             "#define ee(x) x(y)\n"
             "#define f(x) f\n"
             "#define foo e(f(x))\n",
             "foo",
             "f(y)");

  // Regression test: This was failing before.
  CheckExpand(CheckFlag::kPassNaive,
              "#define g(x, y, ...) f(x, y, __VA_ARGS__)\n"
              "#define f(x, y, ...) g(0, x, y, __VA_ARGS__)\n",
              "f(1, 2)",
              "f(0,1,2,)");

  CheckExpand(CheckFlag::kPassNaive,
              "#define F(X) G\n"
              "#define G(Y) Y+3\n",
              "F(1)(2)",
              "2 +3");

  // Each macro does not look like evil, but when combined, not so easy.
  // In CBVExpander, expansion "C() --> D" fails, since D is defined,
  // and not in hideset.
  CheckExpand(CheckFlag::kPassNaive,
              "#define A() C()\n"
              "#define B() ()\n"
              "#define C() D\n"
              "#define D() 1\n"
              "#define FOO A()B()\n"
              "#define ID(X) X\n",
              "ID(FOO)",
              "1");
}

}  // namespace devtools_goma
