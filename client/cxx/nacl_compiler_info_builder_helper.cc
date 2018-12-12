// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nacl_compiler_info_builder_helper.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "file_dir.h"
#include "path.h"
#include "path_resolver.h"

namespace devtools_goma {

#ifdef _WIN32
// static
string NaClCompilerInfoBuilderHelper::GetNaClToolchainRoot(
    const string& normal_nacl_gcc_path) {
  return PathResolver::ResolvePath(
      file::JoinPath(file::Dirname(normal_nacl_gcc_path), ".."));
}
#endif

// static
void NaClCompilerInfoBuilderHelper::CollectPNaClClangResources(
    const string& local_compiler_path,
    const string& cwd,
    std::vector<string>* resource_paths) {
  // If compiler is pnacl, gather all pydir/*.py (don't gather other files.)

  absl::string_view local_compiler_dir = file::Dirname(local_compiler_path);
  std::vector<DirEntry> entries;
  string pydir(file::JoinPath(local_compiler_dir, "pydir"));
  string abs_pydir = file::JoinPathRespectAbsolute(cwd, pydir);
  if (ListDirectory(abs_pydir, &entries)) {
    for (const auto& entry : entries) {
      if (!entry.is_dir && absl::EndsWith(entry.name, ".py")) {
        resource_paths->push_back(file::JoinPath(pydir, entry.name));
      }
    }
  }

  // REV is used for --version.
  resource_paths->push_back(file::JoinPath(local_compiler_dir, "..", "REV"));

  resource_paths->push_back(file::JoinPath(local_compiler_dir, "driver.conf"));

  // subprograms? pnacl-clang needs this, but pnacl-clang++ not? not sure the
  // exact condition.
  resource_paths->push_back(file::JoinPath(local_compiler_dir, "pnacl-llc"));

  // Also, collect all dependent libraries by ldd.
  // Currently, instead using ldd, just list the necessary files.
  // TODO: Really use ldd to collect necessary libraries.
  resource_paths->push_back(
      file::JoinPath(local_compiler_dir, "..", "lib", "libLLVM-3.7svn.so"));
  resource_paths->push_back(
      file::JoinPath(local_compiler_dir, "..", "lib", "libc++.so.1"));
}

// static
void NaClCompilerInfoBuilderHelper::CollectNaClGccResources(
    const string& local_compiler_path,
    const string& cwd,
    std::vector<string>* resource_paths) {
  absl::string_view local_compiler_dir = file::Dirname(local_compiler_path);

  resource_paths->push_back(file::JoinPath(local_compiler_dir, "..", "libexec",
                                           "gcc", "x86_64-nacl", "4.4.3",
                                           "cc1"));
  resource_paths->push_back(file::JoinPath(local_compiler_dir, "..", "libexec",
                                           "gcc", "x86_64-nacl", "4.4.3",
                                           "cc1plus"));
  // this is subprogram?
  // Note this verbose path is actually used in nacl-gcc.
  resource_paths->push_back(file::JoinPath(
      local_compiler_dir, "..", "lib", "gcc", "x86_64-nacl", "4.4.3", "..",
      "..", "..", "..", "x86_64-nacl", "bin", "as"));
}

// static
void NaClCompilerInfoBuilderHelper::CollectNaClClangResources(
    const string& local_compiler_path,
    const string& cwd,
    std::vector<string>* resource_paths) {
  absl::string_view local_compiler_dir = file::Dirname(local_compiler_path);

  // REV is used for --version.
  resource_paths->push_back(file::JoinPath(local_compiler_dir, "..", "REV"));

  resource_paths->push_back(
      file::JoinPath(local_compiler_dir, "..", "lib", "libLLVM-3.7svn.so"));
  resource_paths->push_back(
      file::JoinPath(local_compiler_dir, "..", "lib", "libc++.so.1"));
}

}  // namespace devtools_goma
