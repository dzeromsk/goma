// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "compiler_flags.h"

#include <ctype.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <sstream>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "cmdline_parser.h"
#include "file.h"
#include "file_helper.h"
#include "flag_parser.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "known_warning_options.h"
#include "path.h"
#include "path_resolver.h"
#include "path_util.h"
using std::string;

namespace devtools_goma {

CompilerFlags::CompilerFlags(const std::vector<string>& args, string cwd)
    : args_(args), cwd_(std::move(cwd)), is_successful_(false) {
  CHECK(!args.empty());
  compiler_name_ = args[0];
}

// TODO: wtf
void CompilerFlags::Fail(const string& msg, const std::vector<string>& args) {
  fail_message_ = "Flag parsing failed: " + msg + "\n";
  fail_message_ += "ARGS:\n";
  for (const auto& arg : args) {
    fail_message_ += " " + arg;
  }
  fail_message_ += "\n";
  is_successful_ = false;
}

// static
bool CompilerFlags::ExpandPosixArgs(
    const string& cwd, const std::vector<string>& args,
    std::vector<string>* expanded_args,
    std::vector<string>* optional_input_filenames) {
  for (size_t i = 0; i < args.size(); ++i) {
    const string& arg = args[i];
    bool need_expand = false;
    if (absl::StartsWith(arg, "@")) {
      need_expand = true;

      // MacOSX uses @executable_path, @loader_path or @rpath as prefix
      // of install_name (b/6845420).
      // It could also be a linker rpath (b/31920050).
      bool is_linker_magic_token = false;
      if (absl::StartsWith(arg, "@executable_path/") ||
           absl::StartsWith(arg, "@loader_path/") ||
           absl::StartsWith(arg, "@rpath/")) {
        is_linker_magic_token = true;
      }
      if (is_linker_magic_token &&
          i > 0 &&
          (args[i - 1] == "-rpath" || args[i - 1] == "-install_name")) {
          need_expand = false;
      }
      if (is_linker_magic_token &&
          i > 2 &&
          args[i - 3] == "-Xlinker" &&
          (args[i - 2] == "-rpath" || args[i - 2] == "-install_name") &&
          args[i - 1] == "-Xlinker") {
          need_expand = false;
      }
    }
    if (!need_expand) {
      expanded_args->push_back(arg);
      continue;
    }
    const string& source_list_filename =
        PathResolver::PlatformConvert(arg.substr(1));
    string source_list;
    if (!ReadFileToString(
            file::JoinPathRespectAbsolute(cwd, source_list_filename),
            &source_list)) {
      LOG(WARNING) << "failed to read: " << source_list_filename
                   << " at " << cwd;
      return false;
    }
    if (optional_input_filenames) {
      optional_input_filenames->push_back(source_list_filename);
    }

    if (!ParsePosixCommandLineToArgv(source_list, expanded_args)) {
      LOG(WARNING) << "failed to parse command line: " << source_list;
      return false;
    }
    VLOG(1) << "expanded_args:" << *expanded_args;
  }
  return true;
}

// Return the base name of compiler, such as 'x86_64-linux-gcc-4.3',
// 'g++', derived from compiler_name.
string CompilerFlags::compiler_base_name() const {
  string compiler_base_name = compiler_name_;
  size_t found_slash = compiler_base_name.rfind('/');
  if (found_slash != string::npos) {
    compiler_base_name = compiler_base_name.substr(found_slash + 1);
  }
  return compiler_base_name;
}

string CompilerFlags::DebugString() const {
  std::stringstream ss;
  for (const auto& arg : args_) {
    ss << arg << " ";
  }
  if (!expanded_args_.empty() && args_ != expanded_args_) {
    ss << " -> ";
    for (const auto& arg : expanded_args_) {
      ss << arg << " ";
    }
  }
  return ss.str();
}

void CompilerFlags::GetClientImportantEnvs(
    const char** envp, std::vector<string>* out_envs) const {
  for (const char** e = envp; *e; e++) {
    if (IsClientImportantEnv(*e)) {
      out_envs->push_back(*e);
    }
  }
}

void CompilerFlags::GetServerImportantEnvs(
    const char** envp, std::vector<string>* out_envs) const {
  for (const char** e = envp; *e; e++) {
    if (IsServerImportantEnv(*e)) {
      out_envs->push_back(*e);
    }
  }
}

}  // namespace devtools_goma
