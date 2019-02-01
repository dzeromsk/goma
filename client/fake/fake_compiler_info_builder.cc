// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake/fake_compiler_info_builder.h"

#include "absl/strings/str_split.h"
#include "counterz.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "ioutil.h"
#include "util.h"

namespace devtools_goma {

namespace {

// Parses compiler's output. |compiler_output| is the stdout string of compiler.
// Returns true if succeeded, and |version| will contain compiler version.
// Returns false if failed.
bool ParseFakeCompilerVersion(const string& compiler_output, string* version) {
  // output should be like
  // `fake version 1.0`.

  absl::string_view line = compiler_output;
  if (!absl::ConsumePrefix(&line, "fake ")) {
    return false;
  }
  if (!absl::ConsumePrefix(&line, "version ")) {
    return false;
  }

  *version = string(line);
  return true;
}

// Gets compiler version by invoking `fake` compiler.
// |compiler_path| is a path to `fake` compiler.
// |compiler_info_envs| is envvars to use to invoke `fake` compiler.
// |cwd| is the current directory.
// If succeeded, returns true and |version| will contain compiler version.
// If failed, returns false. The content of |version| is undefined.
bool GetFakeCompilerVersion(const string& compiler_path,
                            const std::vector<string>& compiler_info_envs,
                            const string& cwd,
                            string* version) {
  const std::vector<string> argv{
      compiler_path, "--version",
  };
  std::vector<string> env(compiler_info_envs);
  env.emplace_back("LC_ALL=C");

  int32_t status = 0;
  string output = ReadCommandOutput(compiler_path, argv, env, cwd,
                                    MERGE_STDOUT_STDERR, &status);
  if (status != 0) {
    LOG(ERROR) << "ReadCommandOutput exited with non zero status code."
               << " compiler_path=" << compiler_path << " status=" << status
               << " argv=" << argv << " env=" << env << " cwd=" << cwd;
    return false;
  }

  return ParseFakeCompilerVersion(output, version);
}

}  // anonymous namespace

void FakeCompilerInfoBuilder::SetLanguageExtension(
    CompilerInfoData* data) const {
  // Sets `fake` data extension.
  // This is required to declare |data| is for `fake` compiler.
  (void)data->mutable_fake();
}

void FakeCompilerInfoBuilder::SetTypeSpecificCompilerInfo(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const string& abs_local_compiler_path,
    const std::vector<string>& compiler_info_envs,
    CompilerInfoData* data) const {
  // In SetTypeSpecificCompilerInfo, we have to set CompilerInfoData fields that
  // are compiler type specific. Especially, we have to set these.
  //   1. data extension
  //   2. compiler version and target

  // Set target.
#ifdef _WIN32
  data->set_target("x86_64-pc-windows-msvc");
#elif defined(__MACH__)
  data->set_target("x86_64-apple-darwin");
#else
  data->set_target("x86_64-unknown-linux-gnu");
#endif

  if (!GetFakeCompilerVersion(local_compiler_path, compiler_info_envs,
                              flags.cwd(), data->mutable_version())) {
    AddErrorMessage(
        "Failed to get fake compiler version for " + local_compiler_path, data);
    return;
  }

  // Add compiler as an resource input.
  CompilerInfoData::ResourceInfo r;
  if (!CompilerInfoBuilder::ResourceInfoFromPath(
          flags.cwd(), local_compiler_path, CompilerInfoData::EXECUTABLE_BINARY,
          &r)) {
    AddErrorMessage(
        "failed to get fake compiler resource info for " + local_compiler_path,
        data);
    return;
  }
  *data->add_resource() = std::move(r);
}

}  // namespace devtools_goma
