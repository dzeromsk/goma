// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "clang_flags_helper.h"

#include "flag_parser.h"

namespace devtools_goma {

ClangFlagsHelper::ClangFlagsHelper(const std::vector<string>& args) {
  FlagParser parser;
  std::vector<string> xclang_flags;
  parser.AddFlag("Xclang")->SetValueOutputWithCallback(nullptr, &xclang_flags);
  parser.Parse(args);

  FlagParser xclang_flag_parser;
  xclang_flag_parser.mutable_options()->has_command_name = false;
  xclang_flag_parser.mutable_options()->allows_equal_arg = true;
  xclang_flag_parser.mutable_options()->allows_nonspace_arg = true;

  FlagParser::Flag* flag_fdebug_compilation_dir =
      xclang_flag_parser.AddFlag("fdebug-compilation-dir");
  xclang_flag_parser.Parse(xclang_flags);

  if (flag_fdebug_compilation_dir->seen()) {
    fdebug_compilation_dir_ = flag_fdebug_compilation_dir->GetLastValue();
  }
}

}  // namespace devtools_goma
