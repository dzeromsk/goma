// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_CBV_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_CBV_H_

#include <vector>

#include "absl/container/inlined_vector.h"
#include "cpp_parser.h"
#include "space_handling.h"

namespace devtools_goma {

// CppMacroExpanderCBV is a macro expander that can handle macros
// that can be expanded by usual programming language way.
// (call-by-value way).
//
// This expander assumes: expression can be always expanded to
// tokens that won't cause any more expansion. This makes
// macro expansion much simple, so we can make a fast macro
// expansion.
//
// ----------------------------------------------------------------------
// OK Example 1:
//
// Defines:
//  #define A() 1
//  #define B A()
// Expand:
//   B
//
// B --> A() --> 1.
// Token [1] cannot be expanded more, so OK.
//
// ----------------------------------------------------------------------
// OK Example 2:
//
// Defines:
//  #define F(X, Y) X + Y
//  #define G(X) X + 1
// Expand:
//  F(G(1), G(2))
//
// G(1) is expanded to 1 + 1, and G(2) is expanded to 2 + 1.
// These don't have any identifier, so no more expansion will happen.
// Then, expand F(1 + 1, 2 + 1).
// It's expanded to 1 + 1 + 2 + 1.
//
// ----------------------------------------------------------------------
// OK Example 3:
//
// Defines:
//  #define A B
// Expand:
//  A
//
// A --> B.
// As long as token [B] is not defined, no more expansion happens.
// So this is OK.
//
// ----------------------------------------------------------------------
// NG Example 1: Higher order function
//
// Defines:
//  #define B() 1
//  #define A() B
//  #define ID(X) X
// Expand:
//  ID(A()())
//
// This should be expanded like:
//   ID(A()()) --> B() --> 1
//
// A() is expanded to B, and Token [B] is defined as a function.
// In expasion A() --> B, B does not have any arguments. The argument is passed
// after the expansion (A() --> B) is finished.
//
// This means higher order function is not supported in this expander.
//
// This pattern can be supported if we change expansion order. However,
// in wild use, this pattern won't happen so much. So not supported yet.
//
// ----------------------------------------------------------------------
// NG Example 2: Higher order function, AST change
//
// Defines:
//  #define A (1, 2)
//  #define B(X, Y) X + Y
//  #define ID(X) X
// Expand:
//  ID(B A).
//
// This should be expanded like:
//   ID(B A) --> B (1, 2) --> 1 + 2
//
//
// When we just see B A, this does not look function invocation.
// In this expander, we require all function invocation should look like
// a function invocation as written.
//
// Basically we require the abstract syntax tree (AST) is kept during
// macro expansion. "B A" does not look like function invocation in AST,
// so we don't support this case.
//
// ----------------------------------------------------------------------
// NG Example 3: unbalanced parens
//
// Defines:
//  #define A (
//  #define G(X, Y) X + Y
//  #define F(X, Y) G(X, Y)
// Expand:
//  F(A, 1)
//
// This should be expanded like:
//   F(A, 1) --> G((, 1) --> fail.
//
// If unbalanced paren appers, expression AST can be changed from what
// we expect. ',' can also cause the same problem.
//
// ----------------------------------------------------------------------
// NG Example 4: wild comma
//
// Defines
//  #define A(X, Y) B(X, Y)
//  #define B(X, Y, Z) X + Y + Z
//  #define C 1, 2
//  #define ID(X) X
// Expand
//  ID(A(C, 3))
//
// This should be expanded like
//   ID(A(C, 3)) --> B(1, 2, 3) --> 1 + 2 + 3
//
// B's argument number is changed. So we render AST is changed in this case.
//
// ----------------------------------------------------------------------
//
// Roughly speaking, after expansion happened, output token should not have
// a token that has all of the following features.
//   1. type is identifier
//   2. macro is defined
//   3. macro is not in hideset
//
// Also, after expansion happened, output token should not change the AST
// which is parsed from only macro definition. ',' or unbalanced '(', ')'
// can change AST.
//
//
// When we detect these, expansion fails. (so fallback must happen).
//
// So, in this expander, we don't support
//  1. # or ##
//      since they produce another token dynamically
//  2. mismatched function argument length (e.g. calling f(1, 2) for f(X))
//  3. ... or __VA_ARGS__
//      also they might produce another token dynamically,
//      Especially, __AR_ARGS__ can have ','.
//  4. unbalanced parens in macro replacements.
//  5. wild ',' usage
//
// Note that these don't happen usually.
// When building chrome, This expander just fallbacks due to '##'
// on Linux. while evaluating macro. The fallback ratio is less than 2%.
class CppMacroExpanderCBV {
 public:
  explicit CppMacroExpanderCBV(CppParser* parser) : parser_(parser) {}

  bool ExpandMacro(const ArrayTokenList& input,
                   SpaceHandling space_handling,
                   ArrayTokenList* output);

 private:
  // Env is an environment while a macro replacement is being expanded.
  // It's a map from a variable to a token list.
  //
  // Example 1.
  //  #define F(X, Y) X + Y
  // and expand F(1, 2).
  // Here, Env is {X |-> {[1]}, Y |-> {[2]}}.
  //
  // Example 2.
  //  #define F(X, Y) X + Y
  //  #define A(X) X+1
  // and expand F(A(1), 2).
  // In this pattern, first we expand A(1).
  // In expanding A(X), Env is {X |-> {[1]}}.
  // Then A(1) is expanded to {[1],[+],[1]}.
  // While expanding F(X, Y), Env is {X |-> {[1],[+],[1]}, Y |-> [2]}.
  //
  // Actually all params are indexed from 0, Env is represented with
  // a vector.
  // The param of InlinedVector (here, 8) is arbitrary chosen.
  // Usually argument list won't be so large. So small number is OK.
  using Env = absl::InlinedVector<ArrayTokenList, 8>;

  using ArgRange = std::pair<ArrayTokenList::const_iterator,
                             ArrayTokenList::const_iterator>;
  using ArgRangeVector = absl::InlinedVector<ArgRange, 8>;

  bool Expand(ArrayTokenList::const_iterator input_begin,
              ArrayTokenList::const_iterator input_end,
              SpaceHandling space_handling,
              const MacroSet& hideset,
              const Env& env,
              ArrayTokenList* output);

  // Get macro arguments using the comma tokens as delimiters.
  // Arguments in nested parenthesis pairs are parsed in nested token lists.
  //
  // |args| will contain the pair of begin iterator and end iterator of each
  // arguments. For example,
  // e.g. macro(a1, a2(b1, b2), a3, a4(c1(d)))
  //  --> [[a1], [a2, '(', b1, b2, ')'], [a3], [a4, '(', c1, '(', d, ')', ')']]
  //
  // |args| must contain |n| arguments if this function succeeds.
  // if argument is short or plenty, false is returned.
  //
  // When true is returned, |*cur| is on last ')' of function arguments.
  // Otherwise, |*cur| becomes undefined.
  static bool GetMacroArguments(ArrayTokenList::const_iterator begin,
                                ArrayTokenList::const_iterator end,
                                int n,
                                ArrayTokenList::const_iterator* cur,
                                ArgRangeVector* args);
  static bool GetMacroArgument(ArrayTokenList::const_iterator begin,
                               ArrayTokenList::const_iterator end,
                               ArrayTokenList::const_iterator* cur,
                               ArrayTokenList::const_iterator* argument_begin,
                               ArrayTokenList::const_iterator* argument_end);

  CppParser* parser_;

  FRIEND_TEST(CppMacroExpanderCBVTest, GetMacroArguments);
  FRIEND_TEST(CppMacroExpanderCBVTest, GetMacroArgumentsEmpty);
  FRIEND_TEST(CppMacroExpanderCBVTest, GetMacroArgumentsEmptyArg);
  FRIEND_TEST(CppMacroExpanderCBVTest, GetMacroArgumentsFail);
  FRIEND_TEST(CppMacroExpanderCBVTest, GetMacroArgumentsPlentyOrShort);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_CBV_H_
