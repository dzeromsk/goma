// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_FAKE_FLAGS_H_
#define DEVTOOLS_GOMA_LIB_FAKE_FLAGS_H_

#include <string>
#include <vector>

#include "lib/compiler_flags.h"
using std::string;

namespace devtools_goma {

class FakeFlags : public CompilerFlags {
 public:
  FakeFlags(const std::vector<string>& args, const string& cwd);

  // Returns the compiler family name.
  string compiler_name() const override { return "fake"; }

  CompilerFlagType type() const override { return CompilerFlagType::Fake; }

  bool IsClientImportantEnv(const char* env) const override { return false; }
  bool IsServerImportantEnv(const char* env) const override { return false; }

  // If |arg| is a fake compiler, it returns true.
  static bool IsFakeCommand(absl::string_view arg);
  // From the first command line argument, returns a compiler family name.
  static string GetCompilerName(absl::string_view arg) { return "fake"; }
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_FAKE_FLAGS_H_
