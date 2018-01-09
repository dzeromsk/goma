// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include_guard_detector.h"

namespace devtools_goma {

void IncludeGuardDetector::OnProcessCondition() {
  ++if_depth_;
  if (if_depth_ > 1)
    return;

  // Non-ifndef condition is found in toplevel.
  ok_ = false;
}

void IncludeGuardDetector::OnProcessIf(const std::string& ident) {
  if (!ident.empty()) {
    OnProcessIfndef(ident);
  } else {
    OnProcessCondition();
  }
}

void IncludeGuardDetector::OnProcessIfndef(const std::string& ident) {
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

void IncludeGuardDetector::OnProcessEndif() {
  --if_depth_;
  if (if_depth_ < 0) {
    // the number of endif is larger than the number of if.
    ok_ = false;
  }
}

void IncludeGuardDetector::OnProcessOther() {
  if (if_depth_ > 0)
    return;

  // toplevel has directives.
  ok_ = false;
}

void IncludeGuardDetector::OnPop() {
  if (if_depth_ != 0) {
    // if/endif is not matched.
    ok_ = false;
  }
}

}  // namespace devtools_goma
