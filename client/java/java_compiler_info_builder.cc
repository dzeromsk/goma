// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "java/java_compiler_info_builder.h"

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "counterz.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "util.h"

namespace devtools_goma {

void JavacCompilerInfoBuilder::SetLanguageExtension(
    CompilerInfoData* data) const {
  (void)data->mutable_javac();
}

void JavacCompilerInfoBuilder::SetTypeSpecificCompilerInfo(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  if (!GetJavacVersion(local_compiler_path, compiler_info_envs, flags.cwd(),
                       data->mutable_version())) {
    AddErrorMessage("Failed to get java version for " + local_compiler_path,
                    data);
    LOG(ERROR) << data->error_message();
    return;
  }
  data->set_target("java");
}

// static
bool JavacCompilerInfoBuilder::ParseJavacVersion(const string& version_info,
                                                 string* version) {
  version->assign(string(absl::StripTrailingAsciiWhitespace(version_info)));
  static const char kJavac[] = "javac ";
  static const size_t kJavacLength = sizeof(kJavac) - 1;  // Removed '\0'.
  if (!absl::StartsWith(*version, kJavac)) {
    LOG(ERROR) << "Unable to parse javac -version output:"
               << *version;
    return false;
  }
  version->erase(0, kJavacLength);
  return true;
}

// static
bool JavacCompilerInfoBuilder::GetJavacVersion(
    const string& javac,
    const std::vector<string>& compiler_info_envs,
    const string& cwd,
    string* version) {
  std::vector<string> argv;
  argv.push_back(javac);
  argv.push_back("-version");
  std::vector<string> env(compiler_info_envs);
  env.push_back("LC_ALL=C");
  int32_t status = 0;
  string javac_out;
  {
    GOMA_COUNTERZ("ReadCommandOutput(version)");
    javac_out =
        ReadCommandOutput(javac, argv, env, cwd, MERGE_STDOUT_STDERR, &status);
  }
  bool ret = ParseJavacVersion(javac_out, version);
  LOG_IF(ERROR, status != 0)
      << "ReadCommandOutput exited with non zero status code."
      << " javac=" << javac
      << " status=" << status
      << " argv=" << argv
      << " env=" << env
      << " cwd=" << cwd;
  return ret;
}

void JavaCompilerInfoBuilder::SetLanguageExtension(
    CompilerInfoData* data) const {
  (void)data->mutable_java();
  LOG(ERROR) << "java is not supported";
}

void JavaCompilerInfoBuilder::SetTypeSpecificCompilerInfo(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
}

}  // namespace devtools_goma
