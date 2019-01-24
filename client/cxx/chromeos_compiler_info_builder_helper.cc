// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos_compiler_info_builder_helper.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"
#include "glog/logging.h"
#include "path.h"

namespace devtools_goma {

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
  // Assuming real_compiler_path filename is like clang-<N>.elf.

  absl::string_view filename = file::Basename(real_compiler_path);
  if (!absl::ConsumePrefix(&filename, "clang-")) {
    return false;
  }
  if (!absl::ConsumeSuffix(&filename, ".elf")) {
    return false;
  }
  if (!absl::SimpleAtoi(filename, version)) {
    return false;
  }

  return true;
}

}  // namespace devtools_goma
