// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "clang_tidy_compiler_info_builder.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "clang_tidy_flags.h"
#include "counterz.h"
#include "cxx/clang_compiler_info_builder_helper.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "path.h"
#include "util.h"

namespace devtools_goma {

void ClangTidyCompilerInfoBuilder::SetTypeSpecificCompilerInfo(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  // Ensure cxx exists.
  (void)data->mutable_cxx();

  if (!GetClangTidyVersionTarget(local_compiler_path, compiler_info_envs,
                                 flags.cwd(), data->mutable_version(),
                                 data->mutable_target())) {
    AddErrorMessage(
        "Failed to get clang-tidy version for " + local_compiler_path, data);
    LOG(ERROR) << data->error_message();
    return;
  }

  string clang_abs_local_compiler_path =
      file::JoinPath(file::Dirname(abs_local_compiler_path), "clang");

  const ClangTidyFlags& clang_tidy_flags =
      static_cast<const ClangTidyFlags&>(flags);

  // See the comment in this function where SetBasicCompilerInfo
  // is called in clangs.is_gcc() if-statement.
  if (!ClangCompilerInfoBuilderHelper::SetBasicCompilerInfo(
          clang_abs_local_compiler_path, clang_tidy_flags.compiler_info_flags(),
          compiler_info_envs, clang_tidy_flags.cwd(), "-x" + flags.lang(), "",
          clang_tidy_flags.is_cplusplus(), clang_tidy_flags.has_nostdinc(),
          data)) {
    DCHECK(data->has_error_message());
    // If error occurred in SetBasicCompilerInfo, we do not need to
    // continue.
    AddErrorMessage(
        "Failed to set basic compiler info for "
        "corresponding clang: " +
            clang_abs_local_compiler_path,
        data);
    LOG(ERROR) << data->error_message();
    return;
  }
}

// static
bool ClangTidyCompilerInfoBuilder::GetClangTidyVersionTarget(
    const string& clang_tidy_path,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    string* version,
    string* target) {
  std::vector<string> argv;
  argv.push_back(clang_tidy_path);
  argv.push_back("-version");

  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");

  int32_t status = 0;
  string output;
  {
    GOMA_COUNTERZ("ReadCommandOutput(version)");
    output = ReadCommandOutput(clang_tidy_path, argv, env, cwd,
                               MERGE_STDOUT_STDERR, &status);
  }

  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " clang_tidy_path=" << clang_tidy_path << " status=" << status
               << " argv=" << argv << " env=" << env << " cwd=" << cwd
               << " output=" << output;
    return false;
  }

  return ParseClangTidyVersionTarget(output, version, target);
}

// static
bool ClangTidyCompilerInfoBuilder::ParseClangTidyVersionTarget(
    const string& output,
    string* version,
    string* target) {
  static const char kVersion[] = "  LLVM version ";
  static const char kTarget[] = "  Default target: ";

  std::vector<string> lines = ToVector(
      absl::StrSplit(output, absl::ByAnyChar("\r\n"), absl::SkipEmpty()));
  if (lines.size() < 4)
    return false;
  if (!absl::StartsWith(lines[1], kVersion))
    return false;
  if (!absl::StartsWith(lines[3], kTarget))
    return false;

  *version = lines[1].substr(strlen(kVersion));
  *target = lines[3].substr(strlen(kTarget));

  return true;
}

}  // namespace devtools_goma
