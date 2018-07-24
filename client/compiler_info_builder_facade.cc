// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compiler_info_builder_facade.h"

#include "glog/logging.h"

namespace devtools_goma {

std::unique_ptr<CompilerInfoData>
CompilerInfoBuilderFacade::FillFromCompilerOutputs(
    const CompilerFlags& flags,
    const string& local_compiler_path,
    const std::vector<string>& compiler_info_envs) {
  switch (flags.type()) {
    case CompilerFlagType::Unknown:
      return nullptr;
    case CompilerFlagType::Gcc: {
      auto data = gcc_builder_.FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
      CHECK(data->has_cxx());
      return data;
    }
    case CompilerFlagType::Clexe: {
      auto data = vc_builder_.FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
      CHECK(data->has_cxx());
      return data;
    }
    case CompilerFlagType::ClangTidy: {
      auto data = clang_tidy_builder_.FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
      CHECK(data->has_cxx());
      return data;
    }
    case CompilerFlagType::Javac: {
      auto data = javac_builder_.FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
      CHECK(data->has_javac());
      return data;
    }
    case CompilerFlagType::Java: {
      auto data = java_builder_.FillFromCompilerOutputs(
          flags, local_compiler_path, compiler_info_envs);
      CHECK(data->has_java());
      return data;
    }
  }
}

}  // namespace devtools_goma
