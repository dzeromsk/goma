// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_NAIVE_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_NAIVE_H_

#include <list>

#include "cpp_macro.h"
#include "cpp_macro_set.h"
#include "cpp_token.h"
#include "gtest/gtest_prod.h"

namespace devtools_goma {

class CppParser;

// Token + HideSet
struct TokenHS {
  TokenHS(CppToken token, MacroSet hideset)
      : token(std::move(token)), hideset(std::move(hideset)) {}

  CppToken token;
  MacroSet hideset;
};

using TokenHSList = std::list<TokenHS>;

struct TokenHSListRange {
  TokenHSListRange() = default;
  // TODO: When trusty's libstdc++ is updated to be C++11 conformant,
  // we can use const_iterator instead.
  TokenHSListRange(TokenHSList::iterator begin, TokenHSList::iterator end)
      : begin(begin), end(end) {}

  TokenHSList::iterator begin;
  TokenHSList::iterator end;
};

class CppMacroExpanderNaive {
 public:
  explicit CppMacroExpanderNaive(CppParser* parser) : parser_(parser) {}

  void ExpandMacro(const ArrayTokenList& input,
                   bool skip_space,
                   ArrayTokenList* output);

 private:
  using ArgVector = std::vector<TokenHSList>;
  enum class GetMacroArgumentsResult {
    kOk,                 // ok
    kNoParen,            // no paren found
    kUnterminatedParen,  // unmatched paren
  };

  bool Expand(TokenHSList* input,
              TokenHSListRange input_range,
              bool skip_space,
              TokenHSList* output);

  bool Substitute(const ArrayTokenList& replacement,
                  const ArgVector& actuals,
                  const MacroSet& hideset,
                  TokenHSList* output);

  bool Glue(TokenHSList* output, const TokenHS& ths);

  static GetMacroArgumentsResult GetMacroArguments(
      const TokenHSListRange& range,
      TokenHSList::iterator* cur,
      std::vector<TokenHSListRange>* arg_ranges);
  static bool GetMacroArgument(const TokenHSListRange& range,
                               TokenHSList::iterator* cur,
                               TokenHSListRange* arg_range);

  static CppToken Stringize(const TokenHSList& list);

  CppParser* parser_;

  FRIEND_TEST(CppMacroExpanderNaiveTest, GetMacroArguments);
  FRIEND_TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsEmpty);
  FRIEND_TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsEmptyArg);
  FRIEND_TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsNoParen);
  FRIEND_TEST(CppMacroExpanderNaiveTest, GetMacroArgumentsUnterminatedParen);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_MACRO_EXPANDER_NAIVE_H_
