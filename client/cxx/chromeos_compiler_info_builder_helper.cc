// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos_compiler_info_builder_helper.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "glog/logging.h"
#include "lib/file_helper.h"
#include "path.h"

#include <unistd.h>

namespace devtools_goma {

namespace {

bool IsKnownClangInChroot(absl::string_view local_compiler_path) {
  return local_compiler_path == "/usr/bin/clang" ||
         local_compiler_path == "/usr/bin/clang++" ||
         local_compiler_path == "/usr/bin/x86_64-cros-linux-gnu-clang" ||
         local_compiler_path == "/usr/bin/x86_64-cros-linux-gnu-clang++";
}

bool ParseEnvdPath(absl::string_view envd_path, string* path) {
  // content is like
  //
  // ```
  // PATH="/usr/x86_64-pc-linux-gnu/x86_64-cros-linux-gnu/gcc-bin/4.9.x"
  // ROOTPATH="/usr/x86_64-pc-linux-gnu/x86_64-cros-linux-gnu/gcc-bin/4.9.x"
  // ```

  string content;
  if (!ReadFileToString(envd_path, &content)) {
    LOG(ERROR) << "failed to open/read " << envd_path;
    return false;
  }

  for (absl::string_view line :
       absl::StrSplit(content, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
    if (absl::ConsumePrefix(&line, "PATH=\"") &&
        absl::ConsumeSuffix(&line, "\"")) {
      *path = string(line);
      return true;
    }
  }

  return false;
}

}  // anonymous namespace

// static
bool ChromeOSCompilerInfoBuilderHelper::IsSimpleChromeClangCommand(
    absl::string_view local_compiler_path,
    absl::string_view real_compiler_path) {
  if (!(absl::EndsWith(local_compiler_path, "clang") ||
        absl::EndsWith(local_compiler_path, "clang++"))) {
    return false;
  }
  if (!absl::EndsWith(real_compiler_path, ".elf")) {
    return false;
  }

  return true;
}

// static
bool ChromeOSCompilerInfoBuilderHelper::CollectSimpleChromeClangResources(
    absl::string_view local_compiler_path,
    absl::string_view real_compiler_path,
    std::vector<string>* resource_paths) {
  absl::string_view local_compiler_dir = file::Dirname(local_compiler_path);

  int version;
  if (!EstimateClangMajorVersion(real_compiler_path, &version)) {
    LOG(ERROR) << "failed to estimate clang major version"
               << " real_compiler_path=" << real_compiler_path;
    return false;
  }

  // if local_compiler is clang++, real_compiler is clang-<N>.elf.
  // However, clang++-<N>.elf and clang-<N> are both necessary to run clang++.
  if (absl::EndsWith(local_compiler_path, "clang++")) {
    resource_paths->push_back(file::JoinPath(
        local_compiler_dir, absl::StrCat("clang++-", version, ".elf")));
    resource_paths->push_back(
        file::JoinPath(local_compiler_dir, absl::StrCat("clang-", version)));
  }

  const string lib_dir = file::JoinPath(local_compiler_dir, "..", "..", "lib");

  resource_paths->push_back(file::JoinPath(lib_dir, "ld-linux-x86-64.so.2"));
  resource_paths->push_back(
      file::JoinPath(lib_dir, absl::StrCat("libLLVM-", version, "svn.so")));
  resource_paths->push_back(file::JoinPath(lib_dir, "libc++.so.1"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libc++abi.so.1"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libm.so.6"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libc.so.6"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libffi.so.6"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libz.so.1"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libdl.so.2"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libtinfo.so.5"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libpthread.so.0"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libxml2.so.2"));

  return true;
}

// static
bool ChromeOSCompilerInfoBuilderHelper::EstimateClangMajorVersion(
    absl::string_view real_compiler_path,
    int* version) {
  // Assuming real_compiler_path filename is like
  // `clang-<N>.elf` or `clang-<N>`.

  absl::string_view filename = file::Basename(real_compiler_path);
  if (!absl::ConsumePrefix(&filename, "clang-")) {
    return false;
  }
  // If this has .elf, remove that.
  // If it doesn't exist, it's not an error.
  absl::ConsumeSuffix(&filename, ".elf");

  if (!absl::SimpleAtoi(filename, version)) {
    return false;
  }

  return true;
}

// static
bool ChromeOSCompilerInfoBuilderHelper::IsClangInChrootEnv(
    absl::string_view local_compiler_path) {
  if (!IsKnownClangInChroot(local_compiler_path)) {
    return false;
  }

  // chromeos chroot env should have /etc/cros_chroot_version.
  if (access("/etc/cros_chroot_version", F_OK) < 0) {
    return false;
  }

  return true;
}

// static
bool ChromeOSCompilerInfoBuilderHelper::CollectChrootClangResources(
    absl::string_view local_compiler_path,
    absl::string_view real_compiler_path,
    std::vector<string>* resource_paths) {
  constexpr absl::string_view lib_dir = "/usr/lib64";

  int version;
  if (!EstimateClangMajorVersion(real_compiler_path, &version)) {
    LOG(ERROR) << "failed to estimate clang major version"
               << " real_compiler_path=" << real_compiler_path;
    return false;
  }

  // TODO: Currently support only target = x86_64.
  // for target=arm, we need to use other resources.
  // check local_compiler_path, and if compiler name looks like arm,
  // we have to use arm-like resources.
  resource_paths->push_back(
      file::JoinPath(lib_dir, absl::StrCat("libLLVM-", version, "svn.so")));
  resource_paths->push_back(file::JoinPath(lib_dir, "libc++.so.1"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libc++abi.so.1"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libffi.so.6"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libxml2.so.2"));
  resource_paths->push_back("/etc/env.d/gcc/.NATIVE");
  resource_paths->push_back("/etc/env.d/05gcc-x86_64-cros-linux-gnu");

  string path_from_envd;
  if (!ParseEnvdPath("/etc/env.d/05gcc-x86_64-cros-linux-gnu",
                     &path_from_envd)) {
    return false;
  }

  if (local_compiler_path == "/usr/bin/x86_64-cros-linux-gnu-clang") {
    // Actually /usr/bin/clang is called.
    // /usr/x86_64-pc-linux-gnu/x86_64-cros-linux-gnu/gcc-bin/4.9.x/x86_64-cros-linux-gnu-clang
    // is wrapper.
    resource_paths->push_back("/usr/bin/clang");
    resource_paths->push_back(
        file::JoinPath(path_from_envd, "x86_64-cros-linux-gnu-clang"));
  } else if (local_compiler_path == "/usr/bin/x86_64-cros-linux-gnu-clang++") {
    // Actually /usr/bin/clang++ is called, and /usr/bin/clang can also be
    // called. The latter 2 binaries are both wrapper.
    resource_paths->push_back("/usr/bin/clang");
    resource_paths->push_back("/usr/bin/clang++");
    resource_paths->push_back(
        file::JoinPath(path_from_envd, "x86_64-cros-linux-gnu-clang"));
    resource_paths->push_back(
        file::JoinPath(path_from_envd, "x86_64-cros-linux-gnu-clang++"));
  }

  return true;
}

// static
void ChromeOSCompilerInfoBuilderHelper::SetAdditionalFlags(
    absl::string_view local_compiler_path,
    google::protobuf::RepeatedPtrField<std::string>* additional_flags) {
  if (local_compiler_path == "/usr/bin/x86_64-cros-linux-gnu-clang" ||
      local_compiler_path == "/usr/bin/x86_64-cros-linux-gnu-clang++") {
    // Wrapper tries to set up ccache, but it's meaningless in goma.
    // we have to set -noccache.
    // TODO: chromeos toolchain should have -noccache by default
    // if goma is enabled.
    additional_flags->Add("-noccache");
  }
}

}  // namespace devtools_goma
