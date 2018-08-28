// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_directive_optimizer.h"

namespace devtools_goma {

namespace {

bool ContainsHasInclude(const std::vector<CppToken>& tokens) {
  for (const auto& t : tokens) {
    if (t.IsIdentifier("__has_include") ||
        t.IsIdentifier("__has_include_next")) {
      return true;
    }
  }

  return false;
}

// Returns true if |directive| is #if and it does not contain
// __has_include or __has_include_next.
bool IsDroppableIf(const CppDirective& directive) {
  if (directive.type() != CppDirectiveType::DIRECTIVE_IF) {
    return false;
  }
  const CppDirectiveIf& directive_if =
      static_cast<const CppDirectiveIf&>(directive);
  return !ContainsHasInclude(directive_if.tokens());
}

// Returns true if |directive| is #elif and it does not contain
// __has_include or __has_include_next.
bool IsDroppableElif(const CppDirective& directive) {
  if (directive.type() != CppDirectiveType::DIRECTIVE_ELIF) {
    return false;
  }
  const CppDirectiveElif& directive_elif =
      static_cast<const CppDirectiveElif&>(directive);
  return !ContainsHasInclude(directive_elif.tokens());
}

}  // namespace

// static
StatsCounter CppDirectiveOptimizer::total_directives_count_;
// static
StatsCounter CppDirectiveOptimizer::if_directives_count_;
// static
StatsCounter CppDirectiveOptimizer::converted_count_;
// static
StatsCounter CppDirectiveOptimizer::dropped_count_;

// static
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
    //
    // However, if #if contains __has_include(X) or __has_include_next(X),
    // we should keep X, so we cannot remove it. b/112669612

    if (d->type() == CppDirectiveType::DIRECTIVE_ENDIF) {
      while (!result.empty() &&
             (IsDroppableElif(*result.back()) ||
              result.back()->type() == CppDirectiveType::DIRECTIVE_ELSE)) {
        result.pop_back();
        ++dropped;
      }

      if (!result.empty() &&
          (IsDroppableIf(*result.back()) ||
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
