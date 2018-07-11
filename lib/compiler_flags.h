// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_H_
#define DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>


#include "absl/strings/string_view.h"
#include "compiler_flag_type.h"
#include "flag_parser.h"
using std::string;

namespace devtools_goma {

class CompilerFlags {
 public:
  virtual ~CompilerFlags() {}

  const std::vector<string>& args() const { return args_; }
  const std::vector<string>& expanded_args() const { return expanded_args_; }

  const std::vector<string>& output_files() const { return output_files_; }
  const std::vector<string>& output_dirs() const { return output_dirs_; }

  const std::vector<string>& input_filenames() const {
    return input_filenames_;
  }
  const std::vector<string>& optional_input_filenames() const {
    return optional_input_filenames_;
  }

  string compiler_base_name() const;
  string implicit_macros() const { return implicit_macros_; }

  bool is_successful() const { return is_successful_; }
  const string& fail_message() const { return fail_message_; }

  virtual string compiler_name() const = 0;

  virtual string lang() const { return lang_; }
  virtual CompilerFlagType type() const = 0;

  // Returns true if the |env| is important for compiler_proxy running env.
  // This will be sent from gomacc to compiler_proxy.
  virtual bool IsClientImportantEnv(const char* env) const = 0;
  // Returns true if the |env| is important for goma backend.
  // This will be sent from compiler_proxy to goma backend.
  // All of server important envs must be client important, too.
  virtual bool IsServerImportantEnv(const char* env) const = 0;

  // Finds client important environment variables, which change the behavior
  // of this compiler into out_envs.
  void GetClientImportantEnvs(const char** envp,
                              std::vector<string>* out_envs) const;
  void GetServerImportantEnvs(const char** envp,
                              std::vector<string>* out_envs) const;


  const string& cwd() const { return cwd_; }
  // In clang-tidy case, the directory in which IncludeProcessor will run
  // is not necessarily the same as cwd().
  virtual const string& cwd_for_include_processor() const { return cwd_; }

  // The flags which changes the result of gcc -v (e.g., system include paths).
  const std::vector<string>& compiler_info_flags() const {
    return compiler_info_flags_;
  }
  // The flags which looks like a flag, but that we don't know.
  const std::vector<string>& unknown_flags() const {
    return unknown_flags_;
  }

  string DebugString() const;

  // Expands @response_file in |args| and sets in |expand_args| and
  // |optional_input_filenames| on posix environments (for gcc/javac).
  // TODO: refactor to support windows platform.
  // Returns true if successful.  Note that it also returns true if |args|
  // doesn't contains @response_file.
  // Returns false if some error.
  static bool ExpandPosixArgs(const string& cwd,
                              const std::vector<string>& args,
                              std::vector<string>* expand_args,
                              std::vector<string>* optional_input_filenames);

 protected:
  enum FlagType {
    kNormal,  // A flag will be added with AddFlag
    kPrefix,  // A flag will be added with AddPrefixFlag
    kBool,    // A flag will be added with AddBoolFlag
  };

  template <bool is_defined>
  class MacroStore : public FlagParser::Callback {
   public:
    explicit MacroStore(std::vector<std::pair<string, bool>>* macros)
        : macros_(macros) {}

    // Returns parsed flag value of value for flag.
    string ParseFlagValue(const FlagParser::Flag& /* flag */,
                          const string& value) override {
      macros_->push_back(std::make_pair(value, is_defined));
      return value;
    }

   private:
    std::vector<std::pair<string, bool>>* macros_;
  };

  CompilerFlags(const std::vector<string>& args, string cwd);
  void Fail(const string& msg, const std::vector<string>& args);

  std::vector<string> args_;
  std::vector<string> expanded_args_;
  // Storing target filename for output related flags.
  std::vector<string> output_files_;
  // Storing directory names specified as output directory.
  // e.g. javac's -d option and -s option.
  std::vector<string> output_dirs_;
  string compiler_name_;
  std::vector<string> input_filenames_;
  std::vector<string> optional_input_filenames_;
  string cwd_;
  std::vector<string> compiler_info_flags_;
  string lang_;
  std::vector<string> unknown_flags_;

  bool is_successful_;
  string fail_message_;
  string implicit_macros_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_H_
