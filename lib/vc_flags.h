// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_VC_FLAGS_H_
#define DEVTOOLS_GOMA_LIB_VC_FLAGS_H_

#include <string>
#include <vector>


#include "absl/strings/string_view.h"
#include "cxx_flags.h"
#include "flag_parser.h"
using std::string;

namespace devtools_goma {

class VCFlags : public CxxFlags {
 public:
  VCFlags(const std::vector<string>& args, const string& cwd);

  const std::vector<string>& include_dirs() const { return include_dirs_; }
  const std::vector<string>& root_includes() const { return root_includes_; }
  const std::vector<std::pair<string, bool>>& commandline_macros() const {
    return commandline_macros_;
  }

  bool is_cplusplus() const override { return is_cplusplus_; }
  bool ignore_stdinc() const { return ignore_stdinc_; }
  bool require_mspdbserv() const { return require_mspdbserv_; }
  bool has_Brepro() const { return has_Brepro_; }

  string compiler_name() const override;

  CompilerFlagType type() const override { return CompilerFlagType::Clexe; }

  bool IsClientImportantEnv(const char* env) const override;
  bool IsServerImportantEnv(const char* env) const override;

  static void DefineFlags(FlagParser* parser);
  static bool ExpandArgs(const string& cwd,
                         const std::vector<string>& args,
                         std::vector<string>* expanded_args,
                         std::vector<string>* optional_input_filenames);

  const string& creating_pch() const { return creating_pch_; }
  const string& using_pch() const { return using_pch_; }
  const string& using_pch_filename() const { return using_pch_filename_; }
  const string& resource_dir() const { return resource_dir_; }

  const string& implicit_macros() const { return implicit_macros_; }

  static bool IsClangClCommand(absl::string_view arg);
  static bool IsVCCommand(absl::string_view arg);
  static string GetCompilerName(absl::string_view arg);

 private:
  friend class VCFlagsTest;
  // Compose output file path
  static string ComposeOutputFilePath(const string& input_file_name,
                                      const string& output_file_or_dir,
                                      const string& output_file_ext);

  std::vector<string> include_dirs_;
  std::vector<string> root_includes_;
  // The second value is true if the macro is defined and false if undefined.
  std::vector<std::pair<string, bool>> commandline_macros_;
  bool is_cplusplus_;
  bool ignore_stdinc_;
  bool has_Brepro_;
  string creating_pch_;
  string using_pch_;
  // The filename of .pch, if specified.
  string using_pch_filename_;
  bool require_mspdbserv_;
  string resource_dir_;
  string implicit_macros_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_VC_FLAGS_H_
