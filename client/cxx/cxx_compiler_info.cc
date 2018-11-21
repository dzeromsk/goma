// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cxx_compiler_info.h"

#include "cxx/include_processor/cpp_directive_parser.h"
#include "glog/logging.h"
#include "path.h"
#include "path_util.h"

namespace devtools_goma {

CxxCompilerInfo::CxxCompilerInfo(std::unique_ptr<CompilerInfoData> data)
    : CompilerInfo(std::move(data)) {
  LOG_IF(DFATAL, !data_->has_cxx())
      << "No C/C++ extension data was found in CompilerInfoData.";

  for (const auto& p : data_->cxx().quote_include_paths()) {
    quote_include_paths_.push_back(p);
  }
  for (const auto& p : data_->cxx().cxx_system_include_paths()) {
    cxx_system_include_paths_.push_back(p);
  }
  for (const auto& p : data_->cxx().system_include_paths()) {
    system_include_paths_.push_back(p);
  }
  for (const auto& p : data_->cxx().system_framework_paths()) {
    system_framework_paths_.push_back(p);
  }

  for (const auto& m : data_->cxx().supported_predefined_macros()) {
    if (!supported_predefined_macros_.insert(make_pair(m, false)).second) {
      LOG(WARNING) << "duplicated predefined_macro: "
                   << " real_compiler_path=" << data_->real_compiler_path()
                   << " macro=" << m;
    }
  }
  for (const auto& m : data_->cxx().hidden_predefined_macros()) {
    if (!supported_predefined_macros_.insert(make_pair(m, true)).second) {
      LOG(WARNING) << "duplicated predefined_macro: "
                   << " real_compiler_path=" << data_->real_compiler_path()
                   << " macro=" << m;
    }
  }
  for (const auto& p : data_->cxx().has_feature()) {
    has_feature_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->cxx().has_extension()) {
    has_extension_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->cxx().has_attribute()) {
    has_attribute_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->cxx().has_cpp_attribute()) {
    has_cpp_attribute_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->cxx().has_declspec_attribute()) {
    has_declspec_attribute_.insert(make_pair(p.key(), p.value()));
  }
  for (const auto& p : data_->cxx().has_builtin()) {
    has_builtin_.insert(make_pair(p.key(), p.value()));
  }

  predefined_directives_ = CppDirectiveParser::ParseFromString(
      predefined_macros(), "<compiler info output>");
}

bool CxxCompilerInfo::IsSystemInclude(const string& filepath) const {
  for (const auto& path : cxx_system_include_paths_) {
    if (HasPrefixDir(filepath, path))
      return true;
  }
  for (const auto& path : system_include_paths_) {
    if (HasPrefixDir(filepath, path))
      return true;
  }
  for (const auto& path : system_framework_paths_) {
    if (HasPrefixDir(filepath, path))
      return true;
  }
  return false;
}

bool CxxCompilerInfo::DependsOnCwd(const string& cwd) const {
  if (CompilerInfo::DependsOnCwd(cwd)) {
    return true;
  }

  for (size_t i = 0; i < quote_include_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(quote_include_paths_[i]) ||
        HasPrefixDir(quote_include_paths_[i], cwd)) {
      VLOG(1) << "quote_include_path[" << i
              << "] is cwd relative:" << quote_include_paths_[i] << " @" << cwd;
      return true;
    }
  }
  for (size_t i = 0; i < cxx_system_include_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(cxx_system_include_paths_[i]) ||
        HasPrefixDir(cxx_system_include_paths_[i], cwd)) {
      VLOG(1) << "cxx_system_include_path[" << i
              << "] is cwd relative:" << cxx_system_include_paths_[i] << " @"
              << cwd;
      return true;
    }
  }
  for (size_t i = 0; i < system_include_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(system_include_paths_[i]) ||
        HasPrefixDir(system_include_paths_[i], cwd)) {
      VLOG(1) << "system_include_path[" << i
              << "] is cwd relative:" << system_include_paths_[i] << " @"
              << cwd;
      return true;
    }
  }
  for (size_t i = 0; i < system_framework_paths_.size(); ++i) {
    if (!file::IsAbsolutePath(system_framework_paths_[i]) ||
        HasPrefixDir(system_framework_paths_[i], cwd)) {
      VLOG(1) << "system_framework_path[" << i
              << "] is cwd relative:" << system_framework_paths_[i] << " @"
              << cwd;
      return true;
    }
  }
  if (data_->cxx().predefined_macros().find(cwd) != string::npos) {
    VLOG(1) << "predefined macros contains cwd " << cwd;
    return true;
  }

  return false;
}

}  // namespace devtools_goma
