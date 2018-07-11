// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_JAVA_FLAGS_H_
#define DEVTOOLS_GOMA_LIB_JAVA_FLAGS_H_

#include <string>
#include <vector>


#include "compiler_flags.h"
using std::string;

namespace devtools_goma {

class JavacFlags : public CompilerFlags {
 public:
  JavacFlags(const std::vector<string>& args, const string& cwd);

  string compiler_name() const override { return "javac"; }

  CompilerFlagType type() const override { return CompilerFlagType::Javac; }

  bool IsClientImportantEnv(const char* env) const override { return false; }
  bool IsServerImportantEnv(const char* env) const override { return false; }

  static void DefineFlags(FlagParser* parser);
  static bool IsJavacCommand(absl::string_view arg);
  static string GetCompilerName(absl::string_view arg);

  const std::vector<string>& jar_files() const { return jar_files_; }

  const std::vector<string>& processors() const { return processors_; }

 private:
  friend class JavacFlagsTest;

  std::vector<string> jar_files_;
  std::vector<string> processors_;
};

class JavaFlags : public CompilerFlags {
 public:
  JavaFlags(const std::vector<string>& args, const string& cwd);

  string compiler_name() const override { return "java"; }

  CompilerFlagType type() const override { return CompilerFlagType::Java; }

  bool IsClientImportantEnv(const char* env) const override { return false; }
  bool IsServerImportantEnv(const char* env) const override { return false; }

  static void DefineFlags(FlagParser* parser);
  static bool IsJavaCommand(absl::string_view arg);
  static string GetCompilerName(absl::string_view arg) { return "java"; }
  const std::vector<string>& jar_files() const { return jar_files_; }

 private:
  std::vector<string> jar_files_;
};

// Parses list of given class paths, and appends .jar and .zip to |jar_files|.
// Note: |jar_files| will not be cleared inside, and the output will be
// appended.
void ParseJavaClassPaths(const std::vector<string>& class_paths,
                         std::vector<string>* jar_files);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_JAVA_FLAGS_H_
