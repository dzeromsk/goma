// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_include_processor.h"

#include "absl/strings/match.h"
#include "path.h"

namespace devtools_goma {

bool FakeIncludeProcessor::Run(const string& trace_id,
                               const FakeFlags& fake_flags,
                               const FakeCompilerInfo& compiler_info,
                               std::set<string>* required_files) {
  if (fake_flags.input_filenames().empty()) {
    return false;
  }

  const string& input = fake_flags.input_filenames()[0];
  // fake compiler's include processor fails if the input filename contains
  // `fail`.
  if (absl::StrContains(file::Basename(input), "fail")) {
    return false;
  }

  // Otherwise, add "success.txt" as required_files.
  required_files->insert("success.txt");
  return true;
}

}  // namespace devtools_goma
