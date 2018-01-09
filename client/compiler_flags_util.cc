// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef _WIN32
// TODO: Check if we need to do this for VCFlags as well.

#include "compiler_flags_util.h"

#include "compiler_flags.h"
#include "compiler_info.h"
#include "compiler_specific.h"
#include "flag_parser.h"
#include "path_resolver.h"

namespace {

class FixPath : public FlagParser::Callback {
 public:
  explicit FixPath(const string& cwd) : cwd_(cwd) {}

  void RegisterSystemPath(const string& path) {
    path_resolver_.RegisterSystemPath(path);
  }

  string ParseFlagValue(const FlagParser::Flag& flag ALLOW_UNUSED,
                        const string& value) override {
    if (path_resolver_.IsSystemPath(value))
      return value;

    return path_resolver_.WeakRelativePath(value, cwd_);
  }

 private:
  string cwd_;
  devtools_goma::PathResolver path_resolver_;
};

}  // anonymous namespace

namespace devtools_goma {

std::vector<string> CompilerFlagsUtil::MakeWeakRelative(
    const std::vector<string>& args,
    const string& cwd,
    const CompilerInfo& compiler_info) {
  FixPath fix_path(cwd);
  for (const auto& path : compiler_info.cxx_system_include_paths())
    fix_path.RegisterSystemPath(path);
  for (const auto& path : compiler_info.system_include_paths())
    fix_path.RegisterSystemPath(path);
  for (const auto& path : compiler_info.system_framework_paths())
    fix_path.RegisterSystemPath(path);

  FlagParser parser;
  GCCFlags::DefineFlags(&parser);

  parser.AddFlag("o")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("MF")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("Wp,-MD,")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("isysroot")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("isystem")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("-isysroot")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("B")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("iframework")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("I")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("F")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("L")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("include")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("imacros")->SetCallbackForParsedArgs(&fix_path);
  parser.AddFlag("MT")->SetCallbackForParsedArgs(&fix_path);
  parser.AddNonFlag()->SetCallbackForParsedArgs(&fix_path);

  parser.AddFlag("Xclang")->SetCallbackForParsedArgs(&fix_path);
  parser.Parse(args);

  return parser.GetParsedArgs();
}

}  // namespace devtools_goma

#endif  // _WIN32
