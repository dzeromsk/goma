// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// fake is a fake program to work like a compiler.
// It just rename *.fake to *.out.

#include <iostream>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "base/filesystem.h"

using std::string;

// convert foo.fake -> foo.out
bool ToOutputName(absl::string_view input_path, string* output_path) {
  if (!absl::ConsumeSuffix(&input_path, ".fake")) {
    return false;
  }

  *output_path = absl::StrCat(input_path, ".out");
  return true;
}

int main(int argc, char* argv[]) {
  // `fake --version` dumps fake compiler version.
  if (argc == 2 && argv[1] == absl::string_view("--version")) {
    std::cout << "fake version 1.0" << std::endl;
    return 0;
  }

  // converts *.fake to *.out.
  for (int i = 1; i < argc; ++i) {
    string output_path;
    if (!ToOutputName(argv[i], &output_path)) {
      std::cerr << "failed to convert *.fake to *.out." << std::endl
                << "input filename must have extension 'fake'." << std::endl
                << "input=" << argv[i] << std::endl;
      return 1;
    }

    if (!file::Copy(argv[i], output_path.c_str(), file::Overwrite()).ok()) {
      std::cerr << "failed to copy " << argv[i] << " to " << output_path
                << std::endl;
      return 1;
    }
  }

  return 0;
}
