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
#include "lib/compiler_flag_type.h"
#include "lib/flag_parser.h"
using std::string;

namespace devtools_goma {

// CompilerFlags is a compiler's command line and its parsed result.
class CompilerFlags {
 public:
  virtual ~CompilerFlags() {}

  // see field's comment below.
  const std::vector<string>& args() const { return args_; }
  // see field's comment below.
  const std::vector<string>& expanded_args() const { return expanded_args_; }

  // see field's comment below.
  const std::vector<string>& output_files() const { return output_files_; }
  // see field's comment below.
  const std::vector<string>& output_dirs() const { return output_dirs_; }

  // see field's comment below.
  const std::vector<string>& input_filenames() const {
    return input_filenames_;
  }
  // see field's comment below.
  const std::vector<string>& optional_input_filenames() const {
    return optional_input_filenames_;
  }

  // see field's comment below.
  bool is_successful() const { return is_successful_; }
  // see field's comment below.
  const string& fail_message() const { return fail_message_; }

  // Returns compiler's base name.
  // e.g.
  //   "gcc" for "/usr/bin/gcc"
  //   "x86_64-linux-gcc-7" for "/usr/bin/x86_64-linux-gcc-7"
  string compiler_base_name() const;
  // Returns compiler's family name.
  // e.g.
  //   "gcc" for "/usr/bin/gcc",
  //   "gcc" for "/usr/bin/x86_64-linux-gcc-7",
  //   "g++" for "/usr/bin/g++".
  virtual string compiler_name() const = 0;

  // see field's comment below.
  virtual string lang() const { return lang_; }
  // Returns CompilerFlagType. The derived class must own unique
  // CompilerFlagType.
  virtual CompilerFlagType type() const = 0;

  // Returns true if the |env| is an environment variable that is required
  // to run compiler locally.
  //
  // This will be sent from gomacc to compiler_proxy.
  // We say an env var is client important if it needs to be sent from gomacc
  // to compiler_proxy.
  virtual bool IsClientImportantEnv(const char* env) const = 0;
  // Returns true if the |env| is an environment variable that is required
  // to run compiler remotely (= in goma server).
  //
  // This will be sent from compiler_proxy to goma server.
  // All of server important envs must be client important, too.
  // We say an env var is server important if it needs to be sent from
  // compiler_proxy to goma server.
  virtual bool IsServerImportantEnv(const char* env) const = 0;

  // Copy client important environment variables from |envp| to |out_envs|.
  void GetClientImportantEnvs(const char** envp,
                              std::vector<string>* out_envs) const;
  // Copy server important environment variables from |envp| to |out_envs|.
  void GetServerImportantEnvs(const char** envp,
                              std::vector<string>* out_envs) const;

  // see field's comment below.
  const string& cwd() const { return cwd_; }
  // If include processor's cwd should be different from cwd, you can
  // override it.
  // In clang-tidy case, the directory in which IncludeProcessor will run
  // is not necessarily the same as cwd().
  virtual const string& cwd_for_include_processor() const { return cwd_; }

  // see field's comment below.
  const std::vector<string>& compiler_info_flags() const {
    return compiler_info_flags_;
  }
  // see field's comment below.
  const std::vector<string>& unknown_flags() const {
    return unknown_flags_;
  }

  // Dump arguments for debugging.
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

  CompilerFlags(const std::vector<string>& args, string cwd);
  // A utility function to indicate parsing was failed.
  void Fail(const string& msg, const std::vector<string>& args);

  // Command line arguments. @rsp file isn't expanded.
  // e.g.
  // ["gcc", "-c", "foo.cc"]
  // ["clang-cl", "@foo.rsp", "/c", "foo.cc"]
  std::vector<string> args_;
  // Expanded command line arguments if the command line contains @rsp.
  // arguments. If @rsp does not exist, this can be empty.
  // e.g.
  // ["clang-cl", "/EHsc", "/c", "foo.cc"]
  std::vector<string> expanded_args_;

  // The list of output files that are expected to be generated by running
  // the given command line.
  //
  // If it's hard to infer the correct output files but it's possible to infer
  // output directories, you can use output_dirs_ instead.
  std::vector<string> output_files_;
  // The list of output directories.
  // Sometimes it's hard to infer all output files (javac-like language will
  // make .class file according to class name). In that case, you can specify
  // output directories instead. e.g. javac's -d option and -s option.
  std::vector<string> output_dirs_;

  // A compiler family name.
  // for example:
  //   "gcc" for "/usr/bin/gcc", "/usr/bin/x86_64-linux-gcc-7"
  //   "g++" for "/usr/bin/g++", "/usr/bin/x86_64-linux-g++-7"
  //   "clang++" for "./Release+Assets/bin/clang++"
  string compiler_name_;

  // The input files detected from command line.
  // e.g. ["gcc", "-c", "foo.cc"] --> ["foo.cc"].
  // If they don't exist, the compile will fail locally (a compile request won't
  // be sent to the goma server.)
  //
  // Implementation Note: In C/C++ case, the current implementation assumes
  // input_filenames_ are all C/C++ sources. If there is a mondatory file
  // but non C/C++ source (e.g. -fmodule-file=<file>), it is not included here
  // but passed with another variable.
  std::vector<string> input_filenames_;

  // The list of optional input files.
  // If it's known a file which is not in command line can also be used,
  // add it to optional input files.
  //
  // goma client will send these input files to goma server if they exist.
  // Even if they don't exist, compiler_proxy will proceed the tasks.
  //
  // e.g. ["gcc", "-fsanitize=memory", "-c", "foo.cc"]
  //      --> ["<resource-dir>/share/asan_blacklist.txt",
  //           "<resource-dir>/asan_blacklist.txt"]
  std::vector<string> optional_input_filenames_;

  // The current working directory of compile command (not of compiler_proxy).
  string cwd_;
  // compiler_info_flags are used for a cache key of CompilerInfoCache.
  // All command line arguments that can affect CompilerInfo should be
  // extracted to compiler_info_flags.
  //
  // For gcc, the flags which changes the result of `gcc -v`.
  // For example, system include paths, predefined macros, etc..
  std::vector<string> compiler_info_flags_;
  // Language type. e.g. "c", "c++", "java", "javac".
  string lang_;
  // The flags which looks like a flag, but that we don't know.
  // e.g. ["-foo"]
  std::vector<string> unknown_flags_;

  // True if compiler flag parsing was succeeded.
  bool is_successful_;
  // An error message if parsing was failed. If parsing was succeeded, this
  // should be empty.
  string fail_message_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_H_
