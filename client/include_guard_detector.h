// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_INCLUDE_GUARD_DETECTOR_H_
#define DEVTOOLS_GOMA_CLIENT_INCLUDE_GUARD_DETECTOR_H_

#include <string>

namespace devtools_goma {

class IncludeGuardDetector {
 public:
  IncludeGuardDetector() : ok_(true), if_depth_(0) {}

  IncludeGuardDetector(const IncludeGuardDetector&) = delete;
  void operator=(const IncludeGuardDetector&) = delete;

  const std::string& detected_ident() const {
    return detected_ident_;
  }

  bool IsGuardDetected() const {
    return ok_ && !detected_ident_.empty();
  }

  // Called when #ifdef is found.
  void OnProcessCondition();
  // Called when #if is found.
  // |ident| is include guard identifier; e.g. in `#if !defined(FOO)`,
  // FOO is |ident|. When such identifier cannot be found, ident should
  // be empty.
  // TODO: Consider renaming this method so that the definition of
  // |ident| is clearer.
  void OnProcessIf(const std::string& ident);
  // Called when #ifndef is found.
  void OnProcessIfndef(const std::string& ident);
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
  int if_depth_;
  // Detected include guard identifier.
  std::string detected_ident_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_INCLUDE_GUARD_DETECTOR_H_
