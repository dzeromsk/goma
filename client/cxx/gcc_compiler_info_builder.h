// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_GCC_COMPILER_INFO_BUILDER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_GCC_COMPILER_INFO_BUILDER_H_

#include <map>
#include <ostream>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "autolock_timer.h"
#include "cxx_compiler_info_builder.h"
#include "gcc_flags.h"

using std::string;

namespace devtools_goma {

// GCCCompilerInfoBuilder is a compiler info builder for gcc-like compilers
// e.g. gcc, g++, clang, clang++, pnacl-clang, etc.
// See VCCompilerInfoBuilder for cl.exe and clang-cl.exe.
class GCCCompilerInfoBuilder : public CxxCompilerInfoBuilder {
 public:
  ~GCCCompilerInfoBuilder() override = default;

  void SetTypeSpecificCompilerInfo(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const override;

  void SetCompilerPath(const CompilerFlags& flags,
                       const string& local_compiler_path,
                       const std::vector<string>& compiler_info_envs,
                       CompilerInfoData* data) const override;

  string GetCompilerName(const CompilerInfoData& data) const override;

  // Returns false if GetExtraSubprograms failed to get subprogram
  // info while a subprogram exists.
  static bool GetExtraSubprograms(const string& normal_gcc_path,
                                  const GCCFlags& flags,
                                  const std::vector<string>& compiler_info_envs,
                                  CompilerInfoData* compiler_info);

  // Parses compile flags for subprograms, especially clang plugins.
  static void ParseSubprogramFlags(const string& normal_gcc_path,
                                   const GCCFlags& flags,
                                   std::vector<string>* clang_plugins,
                                   std::vector<string>* B_options,
                                   bool* no_integrated_as);

  // Returns true if |subprogram_paths| contain a path for as (assembler).
  static bool HasAsPath(const std::vector<string>& subprogram_paths);

  // Get real compiler path.
  // See: go/ma/resources-for-developers/goma-compiler-selection-mechanism
  static string GetRealCompilerPath(const string& normal_gcc_path,
                                    const string& cwd,
                                    const std::vector<string>& envs);

 private:
  // Add resource as EXECUTABLE_BINARY. If the resource is a symlink,
  // the symlink and the actual files are both added as resource.
  // |visited_paths| is used not to process the same resource twice.
  //
  // Returns true if succeeded (or ignored).
  // Returns false if an error has occurred.
  static bool AddResourceAsExecutableBinary(
      const string& resource_path,
      const GCCFlags& gcc_flags,
      absl::flat_hash_set<string>* visited_paths,
      CompilerInfoData* data);
  // The same as AddResourceAsExecutableBinary, with limiting symlink follow
  // count.
  static bool AddResourceAsExecutableBinaryInternal(
      const string& resource_path,
      const GCCFlags& gcc_flags,
      int rest_symlink_follow_count,
      absl::flat_hash_set<string>* visited_paths,
      CompilerInfoData* data);
};

};  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_GCC_COMPILER_INFO_BUILDER_H_
