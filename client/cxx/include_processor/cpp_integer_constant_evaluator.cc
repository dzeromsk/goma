// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_integer_constant_evaluator.h"

namespace devtools_goma {

CppIntegerConstantEvaluator::CppIntegerConstantEvaluator(
    const ArrayTokenList& tokens,
    CppParser* parser)
    : tokens_(tokens), iter_(tokens.begin()), parser_(parser) {
  CHECK(parser_);
  VLOG(2) << parser_->DebugStringPrefix() << " Evaluating: "
          << DebugString(TokenList(tokens.begin(), tokens.end()));
}

int CppIntegerConstantEvaluator::Conditional() {
  int v1 = Expression(Primary(), 0);
  while (iter_ != tokens_.end()) {
    if (iter_->IsPuncChar('?')) {
      ++iter_;
      int v2 = Conditional();
      if (iter_ == tokens_.end() || !iter_->IsPuncChar(':')) {
        parser_->Error("syntax error: missing ':' in ternary operation");
        return 0;
      }
      ++iter_;
      int v3 = Conditional();
      return v1 ? v2 : v3;
    }
    break;
  }
  return v1;
}

int CppIntegerConstantEvaluator::Expression(int v1, int min_precedence) {
  while (iter_ != tokens_.end() && iter_->IsOperator() &&
         iter_->GetPrecedence() >= min_precedence) {
    const CppToken& op = *iter_++;
    int v2 = Primary();
    while (iter_ != tokens_.end() && iter_->IsOperator() &&
           iter_->GetPrecedence() > op.GetPrecedence()) {
      v2 = Expression(v2, iter_->GetPrecedence());
    }
    v1 = op.ApplyOperator(v1, v2);
  }
  return v1;
}

int CppIntegerConstantEvaluator::Primary() {
  int result = 0;
  int sign = 1;
  while (iter_ != tokens_.end()) {
    const CppToken& token = *iter_++;
    switch (token.type) {
      case CppToken::IDENTIFIER:
        // If it comes to here without expanded to number, it means
        // identifier is not defined.  Such case should be 0 unless
        // it is the C++ reserved keyword "true".
        if (parser_->is_cplusplus() && token.string_value == "true") {
          // Int value of C++ reserved keyword "true" is 1.
          // See: ISO/IEC 14882:2011 (C++11) 4.5 Integral promotions.
          result = 1;
        }
        break;
      case CppToken::NUMBER:
      case CppToken::CHAR_LITERAL:
        result = token.v.int_value;
        break;
      case CppToken::SUB:
        sign = 0 - sign;
        continue;
      case CppToken::ADD:
        continue;
      case CppToken::PUNCTUATOR:
        switch (token.v.char_value.c) {
          case '(':
            result = GetValue();
            if (iter_ != tokens_.end() && iter_->IsPuncChar(')')) {
              ++iter_;
            }
            break;
          case '!':
            return !Primary();
          case '~':
            return ~Primary();
          default: {
            parser_->Error("unknown unary operator: ", token.DebugString());
            break;
          }
        }
        break;
      default:
        break;
    }
    break;
  }
  return sign * result;
}

}  // namespace devtools_goma
