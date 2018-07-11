// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include_guard_detector.h"

namespace devtools_goma {

namespace {

// We detect include guard if the following form.
//    [!][defined][(][XXX][)]
// or [!][defined][XXX]
string DetectIncludeGuard(const ArrayTokenList& tokens) {
  // Assuming |tokens| does not contains spaces.

  if (tokens.size() == 5 && tokens[0].IsPuncChar('!') &&
      tokens[1].type == CppToken::IDENTIFIER &&
      tokens[1].string_value == "defined" && tokens[2].IsPuncChar('(') &&
      tokens[3].type == CppToken::IDENTIFIER && tokens[4].IsPuncChar(')')) {
    return tokens[3].string_value;
  }

  if (tokens.size() == 3 && tokens[0].IsPuncChar('!') &&
      tokens[1].type == CppToken::IDENTIFIER &&
      tokens[1].string_value == "defined" &&
      tokens[2].type == CppToken::IDENTIFIER) {
    return tokens[2].string_value;
  }

  return string();
}

class State {
 public:
  State() : ok_(true), if_depth_(0) {}

  const string& detected_ident() const { return detected_ident_; }

  bool IsGuardDetected() const { return ok_ && !detected_ident_.empty(); }

  // Called when #ifdef is found.
  void OnProcessCondition();
  // Called when #if is found.
  // |ident| is include guard identifier; e.g. in `#if !defined(FOO)`,
  // FOO is |ident|. When such identifier cannot be found, ident should
  // be empty.
  // TODO: Consider renaming this method so that the definition of
  // |ident| is clearer.
  void OnProcessIf(const string& ident);
  // Called when #ifndef is found.
  void OnProcessIfndef(const string& ident);
  // Called when #else or #elif is found.
  void OnProcessElseElif();
  // Called when #endif is found.
  void OnProcessEndif();
  // Called when other directive is found.
  void OnProcessOther();
  // Called when popping.
  void OnPop();

 private:
  // |ok_| gets false when we failed to detect include guard.
  // For example.
  // 1. Detected any directive other than the pair of ifndef/endif in toplevel.
  // 2. Detected more than one ifndef/endif pair in toplevel.
  // 3. Detected invalid ifndef in toplevel.
  // 4. if/endif is not balanced (more #if than #endif or vice versa.)
  // Even if ok_ is true, it does not mean we detected an include
  // guard. We also need to check detected_ident_ is not empty.
  bool ok_;
  // The current depth of if/endif. We say it is toplevel when if_depth_ == 0.
  int if_depth_ = 0;
  // Detected include guard identifier.
  string detected_ident_;
};

void State::OnProcessCondition() {
  ++if_depth_;
  if (if_depth_ > 1)
    return;

  // Non-ifndef condition is found in toplevel.
  ok_ = false;
}

void State::OnProcessIf(const string& ident) {
  if (!ident.empty()) {
    OnProcessIfndef(ident);
  } else {
    OnProcessCondition();
  }
}

void State::OnProcessIfndef(const string& ident) {
  ++if_depth_;
  if (if_depth_ > 1) {
    // Non toplevel. Just skipping.
    return;
  }

  if (!ok_)
    return;

  if (!detected_ident_.empty()) {
    // already ifndef has been processed.
    // multiple ifndef/endif in toplevel.
    detected_ident_.clear();
    ok_ = false;
    return;
  }

  if (ident.empty()) {
    // ident of ifndef is invalid.
    ok_ = false;
    return;
  }

  detected_ident_ = ident;
}

void State::OnProcessElseElif() {
  // On toplevel, #else or #elif should not appear.
  if (if_depth_ <= 1) {
    ok_ = false;
  }
}

void State::OnProcessEndif() {
  --if_depth_;
  if (if_depth_ < 0) {
    // the number of endif is larger than the number of if.
    ok_ = false;
  }
}

void State::OnProcessOther() {
  if (if_depth_ > 0)
    return;

  // toplevel has directives.
  ok_ = false;
}

void State::OnPop() {
  if (if_depth_ != 0) {
    // if/endif is not matched.
    ok_ = false;
  }
}

}  // anonymous namespace

// static
string IncludeGuardDetector::Detect(const CppDirectiveList& directives) {
  State s;

  for (const auto& d : directives) {
    switch (d->type()) {
      case CppDirectiveType::DIRECTIVE_IFDEF:
        s.OnProcessCondition();
        break;
      case CppDirectiveType::DIRECTIVE_IFNDEF:
        s.OnProcessIfndef(AsCppDirectiveIfndef(*d).name());
        break;
      case CppDirectiveType::DIRECTIVE_IF:
        s.OnProcessIf(DetectIncludeGuard(AsCppDirectiveIf(*d).tokens()));
        break;
      case CppDirectiveType::DIRECTIVE_ELIF:
      case CppDirectiveType::DIRECTIVE_ELSE:
        s.OnProcessElseElif();
        break;
      case CppDirectiveType::DIRECTIVE_ENDIF:
        s.OnProcessEndif();
        break;
      case CppDirectiveType::DIRECTIVE_INCLUDE:
      case CppDirectiveType::DIRECTIVE_IMPORT:
      case CppDirectiveType::DIRECTIVE_INCLUDE_NEXT:
      case CppDirectiveType::DIRECTIVE_DEFINE:
      case CppDirectiveType::DIRECTIVE_UNDEF:
      case CppDirectiveType::DIRECTIVE_PRAGMA:
      case CppDirectiveType::DIRECTIVE_ERROR:
        s.OnProcessOther();
        break;
    }
  }

  s.OnPop();

  if (s.IsGuardDetected()) {
    return s.detected_ident();
  }

  return string();
}

}  // namespace devtools_goma
