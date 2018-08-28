// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/compiler_flag_type_specific.h"

#include "absl/memory/memory.h"
#include "clang_tidy_execreq_normalizer.h"
#include "clang_tidy_flags.h"
#include "compiler_flags.h"
#include "fake_execreq_normalizer.h"
#include "fake_flags.h"
#include "gcc_execreq_normalizer.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "java_execreq_normalizer.h"
#include "java_flags.h"
#include "vc_execreq_normalizer.h"
#include "vc_flags.h"

namespace devtools_goma {

namespace {

CompilerFlagType CompilerFlagTypeFromArg(absl::string_view arg) {
  if (GCCFlags::IsGCCCommand(arg)) {
    return CompilerFlagType::Gcc;
  }
  if (VCFlags::IsVCCommand(arg) || VCFlags::IsClangClCommand(arg)) {
    // clang-cl gets compatible options with cl.exe.
    // See Also: http://clang.llvm.org/docs/UsersManual.html#clang-cl
    return CompilerFlagType::Clexe;
  }
  if (JavacFlags::IsJavacCommand(arg)) {
    return CompilerFlagType::Javac;
  }
  if (JavaFlags::IsJavaCommand(arg)) {
    return CompilerFlagType::Java;
  }
  if (ClangTidyFlags::IsClangTidyCommand(arg)) {
    return CompilerFlagType::ClangTidy;
  }
  if (FakeFlags::IsFakeCommand(arg)) {
    return CompilerFlagType::Fake;
  }

  return CompilerFlagType::Unknown;
}

}  // namespace

// static
CompilerFlagTypeSpecific CompilerFlagTypeSpecific::FromArg(
    absl::string_view arg) {
  CompilerFlagType type = CompilerFlagTypeFromArg(arg);
  if (type == CompilerFlagType::Unknown) {
    LOG(WARNING) << "Unknown compiler type: arg=" << arg;
  }

  return CompilerFlagTypeSpecific(type);
}

std::unique_ptr<CompilerFlags> CompilerFlagTypeSpecific::NewCompilerFlags(
    const std::vector<string>& args,
    const string& cwd) const {
  switch (type_) {
    case CompilerFlagType::Unknown:
      return nullptr;
    case CompilerFlagType::Gcc:
      return absl::make_unique<GCCFlags>(args, cwd);
    case CompilerFlagType::Clexe:
      return absl::make_unique<VCFlags>(args, cwd);
    case CompilerFlagType::ClangTidy:
      return absl::make_unique<ClangTidyFlags>(args, cwd);
    case CompilerFlagType::Javac:
      return absl::make_unique<JavacFlags>(args, cwd);
    case CompilerFlagType::Java:
      return absl::make_unique<JavaFlags>(args, cwd);
    case CompilerFlagType::Fake:
      return absl::make_unique<FakeFlags>(args, cwd);
  }
}

string CompilerFlagTypeSpecific::GetCompilerName(absl::string_view arg) const {
  switch (type_) {
    case CompilerFlagType::Unknown:
      return "";
    case CompilerFlagType::Gcc:
      return GCCFlags::GetCompilerName(arg);
    case CompilerFlagType::Clexe:
      return VCFlags::GetCompilerName(arg);
    case CompilerFlagType::ClangTidy:
      return ClangTidyFlags::GetCompilerName(arg);
    case CompilerFlagType::Javac:
      return JavacFlags::GetCompilerName(arg);
    case CompilerFlagType::Java:
      return JavaFlags::GetCompilerName(arg);
    case CompilerFlagType::Fake:
      return FakeFlags::GetCompilerName(arg);
  }
}

std::unique_ptr<ExecReqNormalizer>
CompilerFlagTypeSpecific::NewExecReqNormalizer() const {
  switch (type_) {
    case CompilerFlagType::Unknown:
      return absl::make_unique<AsIsExecReqNormalizer>();
    case CompilerFlagType::Gcc:
      return absl::make_unique<GCCExecReqNormalizer>();
    case CompilerFlagType::Clexe:
      return absl::make_unique<VCExecReqNormalizer>();
    case CompilerFlagType::ClangTidy:
      return absl::make_unique<ClangTidyExecReqNormalizer>();
    case CompilerFlagType::Javac:
      return absl::make_unique<JavacExecReqNormalizer>();
    case CompilerFlagType::Java:
      return absl::make_unique<JavaExecReqNormalizer>();
    case CompilerFlagType::Fake:
      return absl::make_unique<FakeExecReqNormalizer>();
  }
}

}  // namespace devtools_goma
