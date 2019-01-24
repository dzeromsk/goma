// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nacl_compiler_info_builder_helper.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "file_dir.h"
#include "glog/logging.h"
#include "path.h"
#include "path_resolver.h"

namespace devtools_goma {

namespace {

// Given a clang binary in |clang_dir|, add paths of its library file
// dependencies to |resource_paths|
void CollectClangDependentLibs(absl::string_view clang_dir,
                               std::vector<string>* resource_paths) {
  // Also, collect all dependent libraries by ldd.
  // Currently, instead using ldd, just list the necessary files.
  // TODO: Really use ldd to collect necessary libraries.
#ifdef __linux__
  const string lib_dir = file::JoinPath(clang_dir, "..", "lib");
  resource_paths->push_back(file::JoinPath(lib_dir, "libLLVM-3.7svn.so"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libc++.so.1"));
#elif defined(__MACH__)
  const string lib_dir = file::JoinPath(clang_dir, "..", "lib");
  resource_paths->push_back(file::JoinPath(lib_dir, "libLLVM-3.7svn.dylib"));
  resource_paths->push_back(file::JoinPath(lib_dir, "libc++.1.dylib"));
#elif defined(_WIN32)
  resource_paths->push_back(file::JoinPath(clang_dir, "LLVM-3.7svn.dll"));
  resource_paths->push_back(file::JoinPath(clang_dir, "libstdc++-6.dll"));
  resource_paths->push_back(file::JoinPath(clang_dir, "libgcc_s_sjlj-1.dll"));
  resource_paths->push_back(file::JoinPath(clang_dir, "libwinpthread-1.dll"));
#else
#error "unsupported platform"
#endif
}

}  // namespace

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

#ifdef __linux__
  // subprograms? pnacl-clang needs this, but pnacl-clang++ not? not sure the
  // exact condition.
  resource_paths->push_back(file::JoinPath(local_compiler_dir, "pnacl-llc"));
#elif defined(__MACH__)
  // TODO: Get corresponding Mac paths. For now, let it fall back
  // to local compile.
#elif defined(_WIN32)
  resource_paths->push_back(file::JoinPath(local_compiler_dir, "clang.exe"));
#else
#error "unsupported platform"
#endif

  CollectClangDependentLibs(local_compiler_dir, resource_paths);
}

// static
void NaClCompilerInfoBuilderHelper::CollectNaClGccResources(
    const string& local_compiler_path,
    const string& cwd,
    std::vector<string>* resource_paths) {
  absl::string_view local_compiler_dir = file::Dirname(local_compiler_path);

  const std::string libexec_dir = file::JoinPath(
      local_compiler_dir, "..", "libexec", "gcc", "x86_64-nacl", "4.4.3");
  // this is subprogram?
  // Note this verbose path is actually used in nacl-gcc.
  const std::string nacl_bin_dir = file::JoinPath(
      local_compiler_dir, "..", "lib", "gcc", "x86_64-nacl", "4.4.3", "..",
      "..", "..", "..", "x86_64-nacl", "bin");

#ifdef __linux__
  resource_paths->push_back(file::JoinPath(libexec_dir, "cc1"));
  resource_paths->push_back(file::JoinPath(libexec_dir, "cc1plus"));
  resource_paths->push_back(file::JoinPath(nacl_bin_dir, "as"));
#elif defined(__MACH__)
  // TODO: Get corresponding Mac paths.
#elif defined(_WIN32)
  resource_paths->push_back(file::JoinPath(libexec_dir, "cc1.exe"));
  resource_paths->push_back(file::JoinPath(libexec_dir, "cc1plus.exe"));
  resource_paths->push_back(file::JoinPath(nacl_bin_dir, "as.exe"));
#else
#error "unsupported platform"
#endif
}

// static
void NaClCompilerInfoBuilderHelper::CollectNaClClangResources(
    const string& local_compiler_path,
    const string& cwd,
    std::vector<string>* resource_paths) {
  absl::string_view local_dir = file::Dirname(local_compiler_path);

  // REV is used for --version.
  resource_paths->push_back(file::JoinPath(local_dir, "..", "REV"));

  CollectClangDependentLibs(local_dir, resource_paths);
}

}  // namespace devtools_goma
