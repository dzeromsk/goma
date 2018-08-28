// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fake_flags.h"

#include "absl/strings/match.h"
#include "lib/path_util.h"
using std::string;

namespace devtools_goma {

FakeFlags::FakeFlags(const std::vector<string>& args, const string& cwd)
    : CompilerFlags(args, cwd) {
  // Set language as "fake".
  lang_ = "fake";

  // Everything ends with .fake is an input. and the outputs are *.out.
  for (size_t i = 1; i < args.size(); ++i) {
    const string& arg = args[i];
    if (absl::EndsWith(arg, ".fake")) {
      input_filenames_.push_back(arg);
      output_files_.push_back(arg.substr(0, arg.size() - 4) + "out");
    }
  }

  // Needs to set is_successful_ when correctly FakeFlags is built.
  is_successful_ = true;
}

// static
bool FakeFlags::IsFakeCommand(absl::string_view arg) {
  const absl::string_view stem = GetStem(arg);
  return stem == "fake";
}

}  // namespace devtools_goma
