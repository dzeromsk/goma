// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_directive_optimizer.h"

namespace devtools_goma {

// static
StatsCounter CppDirectiveOptimizer::total_directives_count_;
// static
StatsCounter CppDirectiveOptimizer::if_directives_count_;
// static
StatsCounter CppDirectiveOptimizer::converted_count_;
// static
StatsCounter CppDirectiveOptimizer::dropped_count_;

// staitc
void CppDirectiveOptimizer::DumpStats(std::ostream* os) {
  *os << "directive_optimizer:"
      << " total_directives=" << total_directives_count_.value()
      << " if_directives=" << if_directives_count_.value()
      << " converted=" << converted_count_.value()
      << " dropped=" << dropped_count_.value();
}

// static
void CppDirectiveOptimizer::Optimize(CppDirectiveList* directives) {
  DCHECK(directives);

  CppDirectiveList result;
  result.reserve(directives->size());

  size_t if_directives = 0;
  size_t converted = 0;
  size_t dropped = 0;

  for (auto&& d : *directives) {
    // If there is no directive between #if and #endif, these directives
    // are meaningless for include processor. So, we can remove them.
    // For example:
    //
    // #if A
    //   f();
    // #endif
    //
    // In this case, #if A, #else, #endif can be removed.
    // #if A
    //   f();
    // #else
    //   g();
    // #endif
    //
    // In this case, only #elif can be removed. Even if B holds or not,
    // nothing happens.
    // #if A
    // # define X
    // #elif B
    // #endif

    if (d->type() == CppDirectiveType::DIRECTIVE_ENDIF) {
      while (!result.empty() &&
             (result.back()->type() == CppDirectiveType::DIRECTIVE_ELIF ||
              result.back()->type() == CppDirectiveType::DIRECTIVE_ELSE)) {
        result.pop_back();
        ++dropped;
      }

      if (!result.empty() &&
          (result.back()->type() == CppDirectiveType::DIRECTIVE_IF ||
           result.back()->type() == CppDirectiveType::DIRECTIVE_IFDEF ||
           result.back()->type() == CppDirectiveType::DIRECTIVE_IFNDEF)) {
        result.pop_back();
        ++dropped;
        continue;
      }
    }

    if (d->type() == CppDirectiveType::DIRECTIVE_IF) {
      ++if_directives;

      const CppDirectiveIf& ifd = AsCppDirectiveIf(*d);

      // #if defined(xxx) --> #ifdef xxx
      // "#ifdef xxx" is a bit faster.
      if (ifd.tokens().size() == 4 && ifd.tokens()[0].IsIdentifier("defined") &&
          ifd.tokens()[1].IsPuncChar('(') &&
          ifd.tokens()[2].type == CppToken::IDENTIFIER &&
          ifd.tokens()[3].IsPuncChar(')')) {
        result.emplace_back(
            new CppDirectiveIfdef(ifd.tokens()[2].string_value));
        ++converted;
        continue;
      }

      if (ifd.tokens().size() == 2 && ifd.tokens()[0].IsIdentifier("defined") &&
          ifd.tokens()[1].type == CppToken::IDENTIFIER) {
        result.emplace_back(
            new CppDirectiveIfdef(ifd.tokens()[1].string_value));
        ++converted;
        continue;
      }

      // #if !defined(xxx) --> #ifndef xxx
      // "#ifndef xxx" is a bit faster.
      if (ifd.tokens().size() == 5 && ifd.tokens()[0].IsPuncChar('!') &&
          ifd.tokens()[1].IsIdentifier("defined") &&
          ifd.tokens()[2].IsPuncChar('(') &&
          ifd.tokens()[3].type == CppToken::IDENTIFIER &&
          ifd.tokens()[4].IsPuncChar(')')) {
        result.emplace_back(
            new CppDirectiveIfndef(ifd.tokens()[3].string_value));
        ++converted;
        continue;
      }

      if (ifd.tokens().size() == 3 && ifd.tokens()[0].IsPuncChar('!') &&
          ifd.tokens()[1].IsIdentifier("defined") &&
          ifd.tokens()[2].type == CppToken::IDENTIFIER) {
        result.emplace_back(
            new CppDirectiveIfndef(ifd.tokens()[2].string_value));
        ++converted;
        continue;
      }
    }

    // Otherwise, move d.
    result.push_back(std::move(d));
  }

  total_directives_count_.Add(directives->size());
  if_directives_count_.Add(if_directives);
  converted_count_.Add(converted);
  dropped_count_.Add(dropped);

  *directives = std::move(result);
}

}  // namespace devtools_goma
