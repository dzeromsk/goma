// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro_expander_naive.h"

#include "absl/strings/str_cat.h"
#include "cpp_parser.h"
#include "cpp_tokenizer.h"

namespace devtools_goma {

// Based on https://www.spinellis.gr/blog/20060626/cpp.algo.pdf

namespace {

// Returns iterator that is not SPACE from |it|. |it| is skipped.
// If |it| is end, end is returned.
TokenHSList::iterator NextNonSpaceTokenHSFrom(TokenHSList::iterator it,
                                              TokenHSList::iterator end) {
  if (it == end) {
    return end;
  }
  ++it;
  while (it != end && it->token.type == CppToken::SPACE) {
    ++it;
  }
  return it;
}

ArrayTokenList::const_iterator NextNonSpaceTokenFrom(
    ArrayTokenList::const_iterator it,
    ArrayTokenList::const_iterator end) {
  if (it == end) {
    return end;
  }
  ++it;
  while (it != end && it->type == CppToken::SPACE) {
    ++it;
  }

  return it;
}

}  // anonymous namespace

void CppMacroExpanderNaive::ExpandMacro(const ArrayTokenList& input_tokens,
                                        bool skip_space,
                                        ArrayTokenList* output_tokens) {
  TokenHSList input;
  for (const auto& t : input_tokens) {
    input.emplace_back(t, MacroSet());
  }

  TokenHSList output;
  TokenHSListRange input_range(input.begin(), input.end());
  // TODO: check error?
  Expand(&input, input_range, skip_space, &output);

  for (const auto& o : output) {
    output_tokens->push_back(o.token);
  }
}

bool CppMacroExpanderNaive::Expand(TokenHSList* input,
                                   TokenHSListRange input_range,
                                   bool skip_space,
                                   TokenHSList* output) {
  DCHECK(output);

  while (input_range.begin != input_range.end) {
    if (input_range.begin->token.type != CppToken::IDENTIFIER) {
      if (input_range.begin->token.type != CppToken::SPACE || !skip_space) {
        output->push_back(*input_range.begin);
      }
      ++input_range.begin;
      continue;
    }

    // When expading "defined" here, thet means defined is used in
    // #define (defined in #if should be expanded beforehand in
    // CppParser::EvalCondition).
    //
    // On VC, defined(XXX) is not handled well but defined XXX
    // is handled. See b/6533195.
    if (input_range.begin->token.string_value == "defined") {
      auto next_it =
          NextNonSpaceTokenHSFrom(input_range.begin, input_range.end);

      if (next_it != input_range.end &&
          next_it->token.type == CppToken::IDENTIFIER) {
        // defined XXX.
        int defined = parser_->IsMacroDefined(next_it->token.string_value);
        output->emplace_back(CppToken(defined), MacroSet());

        input_range.begin = next_it;
        ++input_range.begin;
        continue;
      }

      if (!parser_->is_vc() && next_it != input_range.end &&
          next_it->token.IsPuncChar('(')) {
        auto next2_it = NextNonSpaceTokenHSFrom(next_it, input_range.end);
        auto next3_it = NextNonSpaceTokenHSFrom(next2_it, input_range.end);
        if (next2_it != input_range.end && next3_it != input_range.end &&
            next2_it->token.type == CppToken::IDENTIFIER &&
            next3_it->token.IsPuncChar(')')) {
          // defined(XXX)
          int defined = parser_->IsMacroDefined(next2_it->token.string_value);
          output->emplace_back(CppToken(defined), MacroSet());
          input_range.begin = next3_it;
          ++input_range.begin;
          continue;
        }
      }

      // orphan defined. skip.
    }

    // Case 1. input[0] is not a macro or in input[0]'s hide_set.
    const Macro* macro =
        parser_->GetMacro(input_range.begin->token.string_value);
    if (!macro || input_range.begin->hideset.Has(macro)) {
      output->push_back(*input_range.begin);
      ++input_range.begin;
      continue;
    }

    // Case 2. input[0] is an object-like macro ("()-less macro").
    if (macro->type == Macro::OBJ) {
      MacroSet new_hideset(input_range.begin->hideset);
      new_hideset.Set(macro);
      TokenHSList substitute_output;
      if (!Substitute(macro->replacement, ArgVector(), new_hideset,
                      &substitute_output)) {
        return false;
      }

      // Replace the first item with substitute_output.
      if (substitute_output.empty()) {
        input_range.begin = input->erase(input_range.begin);
      } else {
        auto it = input->erase(input_range.begin);
        input_range.begin = substitute_output.begin();
        input->splice(it, substitute_output);
      }
      continue;
    }

    // Case 2'. input[0] is a callback macro.
    if (macro->type == Macro::CBK) {
      DCHECK(macro->callback);
      CppToken result = (parser_->*(macro->callback))();
      output->emplace_back(std::move(result), MacroSet());
      ++input_range.begin;
      continue;
    }

    // Case 3. input[0] is a function-like macro ("()'d macro").
    // Case 3'. input[0] is a function-like callback macro.
    if (macro->type == Macro::FUNC || macro->type == Macro::CBK_FUNC) {
      std::vector<TokenHSListRange> arg_ranges;
      auto it = input_range.begin;
      switch (GetMacroArguments(input_range, &it, &arg_ranges)) {
        case GetMacroArgumentsResult::kOk:
          break;
        case GetMacroArgumentsResult::kNoParen:
          // failed to get actuals. output ident anyway.
          output->push_back(*input_range.begin);
          ++input_range.begin;
          if (macro->type == Macro::CBK_FUNC) {
            // In this case, error only when callback. Otherwise OK.
            parser_->Error("macro is illformed. () is missing");
          }
          continue;
        case GetMacroArgumentsResult::kUnterminatedParen:
          // failed to get actuals. output ident anyway.
          output->push_back(*input_range.begin);
          ++input_range.begin;
          parser_->Error("unterminated argument list");
          continue;
      }

      // Now, *it is on ')'.
      DCHECK(it->token.IsPuncChar(')')) << it->token.DebugString();

      // Check argument size, and make args.
      ArgVector args;
      if (macro->is_vararg) {
        if (macro->num_args < arg_ranges.size()) {
          // valid.
          args.resize(macro->num_args + 1);  // +1 for var_arg.
          for (size_t i = 0; i < macro->num_args; ++i) {
            args[i].assign(arg_ranges[i].begin, arg_ranges[i].end);
          }
          args[macro->num_args].assign(arg_ranges[macro->num_args].begin,
                                       arg_ranges.back().end);
        } else if (macro->num_args == arg_ranges.size()) {
          // valid.
          args.resize(macro->num_args + 1);  // +1 for var_arg.
          for (size_t i = 0; i < macro->num_args; ++i) {
            args[i].assign(arg_ranges[i].begin, arg_ranges[i].end);
          }
          // args[macro->num_args] is empty.
        } else if (macro->num_args == 1) {
          // Here, arg_ranges.size() == 0.
          // args[0] and args[1] should be empty.
          DCHECK_EQ(0, arg_ranges.size());
          args.resize(2);
        } else {
          // argument is too short.
          parser_->Error(
              "macro argument number mismatching with the parameter list");
          LOG(WARNING)
              << "macro argument number mismatching with the parameter list"
              << " macro->is_vararg=" << macro->is_vararg
              << " macro->num_args=" << macro->num_args
              << " arg_ranges.size()=" << arg_ranges.size();
          return false;
        }
      } else {
        if (macro->num_args == arg_ranges.size()) {
          // valid.
          args.resize(macro->num_args);
          for (size_t i = 0; i < arg_ranges.size(); ++i) {
            args[i].assign(arg_ranges[i].begin, arg_ranges[i].end);
          }
        } else if (macro->num_args == 1 && arg_ranges.size() == 0) {
          // For "#define F(X) ...", F() is valid (X = <empty>).
          // args[0] is empty.
          args.resize(1);
        } else {
          // argument is too short or too long
          parser_->Error(
              "macro argument number mismatching with the parameter list");
          LOG(WARNING)
              << "macro argument number mismatching with the parameter list"
              << " macro->is_vararg=" << macro->is_vararg
              << " macro->num_args=" << macro->num_args
              << " arg_ranges.size()=" << arg_ranges.size();
          return false;
        }
      }

      TokenHSList substitute_output;
      if (macro->type == Macro::FUNC) {
        MacroSet new_hideset(input_range.begin->hideset);
        new_hideset.Intersection(it->hideset);
        new_hideset.Set(macro);

        if (!Substitute(macro->replacement, args, new_hideset,
                        &substitute_output)) {
          return false;
        }
      } else {
        // TODO: HideSet information is lost when we pass args to
        // callback_func. Should we expand arguments here?
        ArrayTokenList func_args;
        if (!args.empty()) {
          for (auto jt = args.front().begin(); jt != args.back().end(); ++jt) {
            func_args.push_back(jt->token);
          }
        }
        CppToken result = (parser_->*(macro->callback_func))(func_args);
        substitute_output.emplace_back(result, MacroSet());
      }

      // Replace function calling to substitute_output.
      ++it;  // skip ')'.
      it = input->erase(input_range.begin, it);
      if (substitute_output.empty()) {
        input_range.begin = it;
      } else {
        input_range.begin = substitute_output.begin();
        input->splice(it, substitute_output);
      }
      continue;
    }

    DCHECK(false) << "should not reach here";
  }

  return true;
}

bool CppMacroExpanderNaive::Substitute(const ArrayTokenList& replacement,
                                       const ArgVector& actuals,
                                       const MacroSet& hideset,
                                       TokenHSList* output) {
  DCHECK(output);
  for (auto it = replacement.begin(); it != replacement.end();) {
    // Note: next_it cannot be used instead of ++it to keep whitespace.
    auto next_it = NextNonSpaceTokenFrom(it, replacement.end());

    // Case 1. # param
    if (it->type == CppToken::SHARP && next_it != replacement.end() &&
        next_it->IsMacroParamType()) {
      DCHECK_LT(next_it->v.param_index, actuals.size());
      TokenHS ths(Stringize(actuals[next_it->v.param_index]), MacroSet());
      output->push_back(std::move(ths));
      it = next_it;
      ++it;
      continue;
    }

    // Case 2. ## param
    if (it->type == CppToken::DOUBLESHARP && next_it != replacement.end() &&
        next_it->IsMacroParamType()) {
      DCHECK_LT(next_it->v.param_index, actuals.size());
      if (!actuals[next_it->v.param_index].empty()) {
        if (!Glue(output, actuals[next_it->v.param_index].front())) {
          return false;
        }
        auto jt = actuals[next_it->v.param_index].begin();
        ++jt;
        for (; jt != actuals[next_it->v.param_index].end(); ++jt) {
          output->push_back(*jt);
        }
      }

      it = next_it;
      ++it;
      continue;
    }

    // Case 3. ## token <remainder>
    if (it->type == CppToken::DOUBLESHARP && next_it != replacement.end()) {
      if (!Glue(output, TokenHS(*next_it, MacroSet()))) {
        return false;
      }

      it = next_it;
      ++it;
      continue;
    }

    // Case 4. param ## <remainder>
    if (it->IsMacroParamType() && next_it != replacement.end() &&
        next_it->type == CppToken::DOUBLESHARP) {
      if (actuals[it->v.param_index].empty()) {
        // param ## param2 <remainder> w
        auto next2_it = next_it;
        ++next2_it;
        while (next2_it != replacement.end() &&
               next2_it->type == CppToken::SPACE) {
          ++next2_it;
        }

        if (next2_it != replacement.end() && next2_it->IsMacroParamType()) {
          output->insert(output->end(),
                         actuals[next2_it->v.param_index].begin(),
                         actuals[next2_it->v.param_index].end());
          it = next2_it;
          ++it;
        } else {
          it = next_it;
          ++it;
        }
      } else {
        // ## is processed in next loop.
        output->insert(output->end(), actuals[it->v.param_index].begin(),
                       actuals[it->v.param_index].end());
        ++it;
      }

      continue;
    }

    // Case 5. param <remainder>
    if (it->IsMacroParamType()) {
      bool skip_space = false;
      TokenHSList actual = actuals[it->v.param_index];
      if (!Expand(&actual, TokenHSListRange(actual.begin(), actual.end()),
                  skip_space, output)) {
        return false;
      }
      ++it;
      continue;
    }

    // Case 6. Other cases.
    output->emplace_back(*it, MacroSet());
    ++it;
  }

  // Do hsadd().
  for (auto& ths : *output) {
    ths.hideset.Union(hideset);
  }

  return true;
}

bool CppMacroExpanderNaive::Glue(TokenHSList* output, const TokenHS& ths) {
  if (output->empty()) {
    output->push_back(ths);
    return true;
  }

  // if output->back() is string, glue should fail
  // e.g.
  //  #define GLUE(X, Y) X ## Y
  //  GLUE("foo", "bar")
  // pasting ""foo"" and ""bar"" does not give a valid preprocessing token
  // However,
  //  GLUE("foo",) or GLUE(, "bar") works.

  string s1;
  if (output->back().token.type == CppToken::STRING) {
    s1 = absl::StrCat("\"", output->back().token.string_value, "\"");
  } else {
    s1 = output->back().token.GetCanonicalString();
  }

  string s2;
  if (ths.token.type == CppToken::STRING) {
    s2 = absl::StrCat("\"", ths.token.string_value, "\"");
  } else {
    s2 = ths.token.GetCanonicalString();
  }

  string s = s1 + s2;

  ArrayTokenList tokens;
  if (!CppTokenizer::TokenizeAll(s, true, &tokens)) {
    string error_message =
        "does not give a valid preprocessing token: failed to tokenize: " + s;
    parser_->Error(error_message);
    LOG(WARNING) << error_message;
    return false;
  }
  if (tokens.size() != 1) {
    string error_message =
        "does not give a valid preprocessing token: more than one token: " + s;
    parser_->Error(error_message);
    LOG(WARNING) << error_message;
    return false;
  }

  output->back().hideset.Intersection(ths.hideset);
  output->back().token = std::move(tokens[0]);
  return true;
}

// Stringize the given token list.
// stringize() in http://www.spinellis.gr/blog/20060626/
//
// static
CppToken CppMacroExpanderNaive::Stringize(const TokenHSList& list) {
  string s;
  for (const auto& ths : list) {
    if (ths.token.type == CppToken::STRING) {
      const auto& token = ths.token;
      string temp;
      temp += "\"";
      for (char c : token.string_value) {
        if (c == '\\' || c == '"') {
          temp += '\\';
        }
        temp += c;
      }
      temp += "\"";
      s += temp;
    } else {
      s += ths.token.GetCanonicalString();
    }
  }

  return CppToken(CppToken::STRING, s);
}

// static
CppMacroExpanderNaive::GetMacroArgumentsResult
CppMacroExpanderNaive::GetMacroArguments(
    const TokenHSListRange& range,
    TokenHSList::iterator* cur,
    std::vector<TokenHSListRange>* arg_ranges) {
  if (*cur == range.end) {
    return GetMacroArgumentsResult::kNoParen;
  }

  // Now *cur must be on macro name identifier.
  // skip macro name
  *cur = NextNonSpaceTokenHSFrom(*cur, range.end);

  // Consumes first '('.
  if (*cur == range.end || !(*cur)->token.IsPuncChar('(')) {
    // No '(' is found
    return GetMacroArgumentsResult::kNoParen;
  }

  *cur = NextNonSpaceTokenHSFrom(*cur, range.end);

  if (*cur != range.end && (*cur)->token.IsPuncChar(')')) {
    // no arguments case. e.g. A()
    return GetMacroArgumentsResult::kOk;
  }

  while (*cur != range.end) {
    // Here, *cur is just after '(' or ','.
    TokenHSListRange arg_range;
    if (!GetMacroArgument(TokenHSListRange(*cur, range.end), cur, &arg_range)) {
      // Failed to get Argument.
      return GetMacroArgumentsResult::kUnterminatedParen;
    }

    arg_ranges->push_back(std::move(arg_range));

    // Here, *cur must be at ')' or ','.
    DCHECK((*cur)->token.IsPuncChar(')') || (*cur)->token.IsPuncChar(','))
        << (*cur)->token.DebugString();

    if ((*cur)->token.IsPuncChar(')')) {
      break;
    }
    ++*cur;
  }

  // Now *cur should be on last ')' (or failed to find the last ')').
  // Don't Consume last ')'.
  if (*cur == range.end || !(*cur)->token.IsPuncChar(')')) {
    return GetMacroArgumentsResult::kUnterminatedParen;
  }

  return GetMacroArgumentsResult::kOk;
}

// static
bool CppMacroExpanderNaive::GetMacroArgument(const TokenHSListRange& range,
                                             TokenHSList::iterator* cur,
                                             TokenHSListRange* arg_range) {
  // skip spaces.
  while (*cur != range.end && (*cur)->token.type == CppToken::SPACE) {
    ++*cur;
  }

  arg_range->begin = *cur;

  int paren_depth = 0;
  while (*cur != range.end) {
    if (paren_depth == 0 &&
        ((*cur)->token.IsPuncChar(',') || (*cur)->token.IsPuncChar(')'))) {
      break;
    }

    if ((*cur)->token.IsPuncChar('(')) {
      ++paren_depth;
    } else if ((*cur)->token.IsPuncChar(')')) {
      --paren_depth;
    }
    ++*cur;
  }

  // |*cur| is just ',' or ')'.
  if (*cur == range.end) {
    return false;
  }

  arg_range->end = *cur;

  return paren_depth == 0;
}

}  // namespace devtools_goma
