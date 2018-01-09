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


#include "flag_parser.h"
#include "string_piece.h"
using std::string;

namespace devtools_goma {

class CompilerFlags {
 public:
  // Returns new instance of subclass of CompilerFlags based on |args|.
  // Returns NULL if args is empty or args[0] is unsupported command.
  static std::unique_ptr<CompilerFlags> New(const std::vector<string>& args,
                                            const string& cwd);

  // MustNew is like New but causes FATAL crash if New returns NULL.
  static std::unique_ptr<CompilerFlags> MustNew(const std::vector<string>& args,
                                                const string& cwd);

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
  virtual bool is_gcc() const { return false; }
  virtual bool is_javac() const { return false; }
  virtual bool is_vc() const { return false; }
  virtual bool is_clang_tidy() const { return false; }
  virtual bool is_java() const { return false; }

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

  // True if arg is gcc command name. Note that clang is considered as
  // gcc variant, so IsGCCCommand("clang") returns true.  However, since
  // clang-cl is not compatible with gcc, IsGCCCommand("clang-cl") returns
  // false.
  static bool IsGCCCommand(absl::string_view arg);
  static bool IsClangCommand(absl::string_view arg);
  static bool IsClangClCommand(absl::string_view arg);
  static bool IsVCCommand(absl::string_view arg);
  static bool IsNaClGCCCommand(absl::string_view arg);
  static bool IsPNaClClangCommand(absl::string_view arg);
  static bool IsJavacCommand(absl::string_view arg);
  static bool IsClangTidyCommand(absl::string_view arg);
  static bool IsJavaCommand(absl::string_view arg);
  static string GetCompilerName(absl::string_view arg);

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
  CompilerFlags(const std::vector<string>& args, const string& cwd);
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

class GCCFlags : public CompilerFlags {
 public:
  enum Mode {
    PREPROCESS, COMPILE, LINK
  };

  GCCFlags(const std::vector<string>& args, const string& cwd);

  const std::vector<string> include_dirs() const;
  const std::vector<string>& non_system_include_dirs() const {
    return non_system_include_dirs_;
  }
  const std::vector<string>& root_includes() const { return root_includes_; }
  const std::vector<string>& framework_dirs() const { return framework_dirs_; }

  const std::vector<std::pair<string, bool>>& commandline_macros() const {
    return commandline_macros_;
  }

  string compiler_name() const override;

  Mode mode() const { return mode_; }

  string isysroot() const { return isysroot_; }
  const string& resource_dir() const { return resource_dir_; }
  const std::set<string>& fsanitize() const { return fsanitize_; }
  const std::map<string, string>& fdebug_prefix_map() const {
    return fdebug_prefix_map_;
  }

  bool is_cplusplus() const { return is_cplusplus_; }
  bool has_nostdinc() const { return has_nostdinc_; }
  bool has_no_integrated_as() const { return has_no_integrated_as_; }
  bool has_pipe() const { return has_pipe_; }
  bool has_ffreestanding() const { return has_ffreestanding_; }
  bool has_fno_hosted() const { return has_fno_hosted_; }
  bool has_fno_sanitize_blacklist() const {
    return has_fno_sanitize_blacklist_;
  }
  bool has_fsyntax_only() const { return has_fsyntax_only_; }
  bool has_resource_dir() const { return !resource_dir_.empty(); }
  bool has_wrapper() const { return has_wrapper_; }
  bool is_precompiling_header() const { return is_precompiling_header_; }
  bool is_stdin_input() const { return is_stdin_input_; }

  bool is_gcc() const override { return true; }

  bool IsClientImportantEnv(const char* env) const override;
  bool IsServerImportantEnv(const char* env) const override;

  static void DefineFlags(FlagParser* parser);

  static string GetCompilerName(absl::string_view arg);

  // If we know -Wfoo, returns true for "foo".
  static bool IsKnownWarningOption(absl::string_view option);
  static bool IsKnownDebugOption(absl::string_view v);

 private:
  friend class GCCFlagsTest;
  static string GetLanguage(const string& compiler_name,
                            const string& input_filename);
  // Get file extension of the given |filepath|.
  static string GetFileNameExtension(const string& filepath);

  std::vector<string> remote_flags_;
  std::vector<string> non_system_include_dirs_;
  std::vector<string> root_includes_;
  std::vector<string> framework_dirs_;
  // The second value is true if the macro is defined and false if undefined.
  std::vector<std::pair<string, bool>> commandline_macros_;
  Mode mode_;
  string isysroot_;
  string resource_dir_;
  // -fsanitize can be specified multiple times, and can be comma separated
  // values.
  std::set<string> fsanitize_;
  std::map<string, string> fdebug_prefix_map_;
  bool is_cplusplus_;
  bool has_nostdinc_;
  bool has_no_integrated_as_;
  bool has_pipe_;
  bool has_ffreestanding_;
  bool has_fno_hosted_;
  bool has_fno_sanitize_blacklist_;
  bool has_fsyntax_only_;
  bool has_wrapper_;
  bool is_precompiling_header_;
  bool is_stdin_input_;
};

class JavacFlags : public CompilerFlags {
 public:
  JavacFlags(const std::vector<string>& args, const string& cwd);

  string compiler_name() const override {
    return "javac";
  }

  bool is_javac() const override { return true; }

  bool IsClientImportantEnv(const char* env) const override { return false; }
  bool IsServerImportantEnv(const char* env) const override { return false; }

  static void DefineFlags(FlagParser* parser);
  static string GetCompilerName(absl::string_view arg);

  const std::vector<string>& jar_files() const { return jar_files_; }

  const std::vector<string>& processors() const { return processors_; }

 private:
  friend class JavacFlagsTest;

  std::vector<string> jar_files_;
  std::vector<string> processors_;
};

class VCFlags : public CompilerFlags {
 public:
  VCFlags(const std::vector<string>& args, const string& cwd);

  const std::vector<string>& include_dirs() const { return include_dirs_; }
  const std::vector<string>& root_includes() const { return root_includes_; }
  const std::vector<std::pair<string, bool>>& commandline_macros() const {
    return commandline_macros_;
  }

  bool is_cplusplus() const { return is_cplusplus_; }
  bool ignore_stdinc() const { return ignore_stdinc_; }
  bool require_mspdbserv() const { return require_mspdbserv_; }

  string compiler_name() const override;

  bool is_vc() const override { return true; }

  bool IsClientImportantEnv(const char* env) const override;
  bool IsServerImportantEnv(const char* env) const override;

  static void DefineFlags(FlagParser* parser);
  static bool ExpandArgs(const string& cwd, const std::vector<string>& args,
                         std::vector<string>* expanded_args,
                         std::vector<string>* optional_input_filenames);

  const string& creating_pch() const { return creating_pch_; }
  const string& using_pch() const { return using_pch_; }
  const string& using_pch_filename() const { return using_pch_filename_; }

  static string GetCompilerName(absl::string_view arg);

 private:
  friend class VCFlagsTest;
  // Get file extension of the given |filepath|.
  static string GetFileNameExtension(const string& filepath);
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
  string creating_pch_;
  string using_pch_;
  // The filename of .pch, if specified.
  string using_pch_filename_;
  bool require_mspdbserv_;
};

// ClangTidy will be used like this.
// $ clang-tidy -checks='*' foo.cc -- -I. -std=c++11
// This command line contains options for clang-tidy and options for clang.
// clang options are parsed in the internal |gcc_flags_|.
// When '--' is not given in the command line, compilation database
// (compile_commands.json) is read. Otherwise, compilation database won't
// be used.
class ClangTidyFlags : public CompilerFlags {
 public:
  ClangTidyFlags(const std::vector<string>& args, const string& cwd);

  string compiler_name() const override;
  bool is_clang_tidy() const override { return true; }

  const string& cwd_for_include_processor() const override {
    return gcc_flags_->cwd();
  }

  // Sets the corresponding clang args for IncludeProcessor.
  // These are set in CompilerTask::InitCompilerFlags.
  void SetClangArgs(const std::vector<string>& clang_args, const string& dir);
  void SetCompilationDatabasePath(const string& compdb_path);
  void set_is_successful(bool flag) { is_successful_ = flag; }

  // NOTE: These methods are valid only after SetClangArgs() is called.
  // Calling these before SetClangArgs() will cause undefined behavior.
  const std::vector<string>& non_system_include_dirs() const {
    return gcc_flags_->non_system_include_dirs();
  }
  const std::vector<string>& root_includes() const {
    return gcc_flags_->root_includes();
  }
  const std::vector<string>& framework_dirs() const {
    return gcc_flags_->framework_dirs();
  }
  const std::vector<std::pair<string, bool>>& commandline_macros() const {
    return gcc_flags_->commandline_macros();
  }
  bool is_cplusplus() const { return gcc_flags_->is_cplusplus(); }
  bool has_nostdinc() const { return gcc_flags_->has_nostdinc(); }

  const string& build_path() const { return build_path_; }
  const std::vector<string>& extra_arg() const { return extra_arg_; }
  const std::vector<string>& extra_arg_before() const {
    return extra_arg_before_;
  }

  bool seen_hyphen_hyphen() const { return seen_hyphen_hyphen_; }
  const std::vector<string>& args_after_hyphen_hyphen() const {
    return args_after_hyphen_hyphen_;
  }

  bool IsClientImportantEnv(const char* env) const override { return false; }
  bool IsServerImportantEnv(const char* env) const override { return false; }

  static void DefineFlags(FlagParser* parser);
  static string GetCompilerName(absl::string_view arg);

 private:
  string build_path_;  // the value of option "-p".
  std::vector<string> extra_arg_;
  std::vector<string> extra_arg_before_;

  bool seen_hyphen_hyphen_;
  std::vector<string> args_after_hyphen_hyphen_;

  // Converted clang flag. This should be made in the constructor.
  std::unique_ptr<GCCFlags> gcc_flags_;
};

class JavaFlags : public CompilerFlags {
 public:
  JavaFlags(const std::vector<string>& args, const string& cwd);

  string compiler_name() const override {
    return "java";
  }

  bool is_java() const override { return true; }

  bool IsClientImportantEnv(const char* env) const override { return false; }
  bool IsServerImportantEnv(const char* env) const override { return false; }

  static void DefineFlags(FlagParser* parser);
  static string GetCompilerName(absl::string_view arg) {
    return "java";
  }
  const std::vector<string>& jar_files() const { return jar_files_; }

 private:
  std::vector<string> jar_files_;
};

// Get the version of gcc/clang to fill CommandSpec.
// dumpversion is the result of gcc/clang -dumpversion
// version is the result of gcc/clang --version
string GetCxxCompilerVersionFromCommandOutputs(const string& command,
                                               const string& dumpversion,
                                               const string& version);

// Truncate string at \r\n.
string GetFirstLine(const string& buf);

// Remove a program name from |version| if it comes from gcc/g++.
string NormalizeGccVersion(const string& version);

// Parses list of given class paths, and appends .jar and .zip to |jar_files|.
// Note: |jar_files| will not be cleared inside, and the output will be
// appended.
void ParseJavaClassPaths(const std::vector<string>& class_paths,
                         std::vector<string>* jar_files);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPILER_FLAGS_H_
