// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_type_specific.h"

#include "absl/memory/memory.h"
#include "clang_tidy_flags.h"
#include "compiler_flags.h"
#include "gcc_flags.h"
#include "glog/logging.h"
#include "java_flags.h"
#include "vc_flags.h"

namespace devtools_goma {

namespace {

CompilerType CompilerTypeFromArg(absl::string_view arg) {
  if (GCCFlags::IsGCCCommand(arg)) {
    return CompilerType::Gcc;
  }
  if (VCFlags::IsVCCommand(arg) || VCFlags::IsClangClCommand(arg)) {
    // clang-cl gets compatible options with cl.exe.
    // See Also: http://clang.llvm.org/docs/UsersManual.html#clang-cl
    return CompilerType::Clexe;
  }
  if (JavacFlags::IsJavacCommand(arg)) {
    return CompilerType::Javac;
  }
  if (JavaFlags::IsJavaCommand(arg)) {
    return CompilerType::Java;
  }
  if (ClangTidyFlags::IsClangTidyCommand(arg)) {
    return CompilerType::ClangTidy;
  }

  return CompilerType::Unknown;
}

}  // namespace

// static
CompilerTypeSpecific CompilerTypeSpecific::FromArg(absl::string_view arg) {
  CompilerType type = CompilerTypeFromArg(arg);
  if (type == CompilerType::Unknown) {
    LOG(WARNING) << "Unknown compiler type: arg=" << arg;
  }

  return CompilerTypeSpecific(type);
}

std::unique_ptr<CompilerFlags> CompilerTypeSpecific::NewCompilerFlags(
    const std::vector<string>& args,
    const string& cwd) const {
  switch (type_) {
    case CompilerType::Unknown:
      return nullptr;
    case CompilerType::Gcc:
      return absl::make_unique<GCCFlags>(args, cwd);
    case CompilerType::Clexe:
      return absl::make_unique<VCFlags>(args, cwd);
    case CompilerType::ClangTidy:
      return absl::make_unique<ClangTidyFlags>(args, cwd);
    case CompilerType::Javac:
      return absl::make_unique<JavacFlags>(args, cwd);
    case CompilerType::Java:
      return absl::make_unique<JavaFlags>(args, cwd);
  }
}

string CompilerTypeSpecific::GetCompilerName(absl::string_view arg) const {
  switch (type_) {
    case CompilerType::Unknown:
      return "";
    case CompilerType::Gcc:
      return GCCFlags::GetCompilerName(arg);
    case CompilerType::Clexe:
      return VCFlags::GetCompilerName(arg);
    case CompilerType::ClangTidy:
      return ClangTidyFlags::GetCompilerName(arg);
    case CompilerType::Javac:
      return JavacFlags::GetCompilerName(arg);
    case CompilerType::Java:
      return JavaFlags::GetCompilerName(arg);
  }
}

}  // namespace devtools_goma
