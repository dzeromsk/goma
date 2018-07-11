// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "java_compiler_info.h"

#include "glog/logging.h"

namespace devtools_goma {

JavaCompilerInfo::JavaCompilerInfo(std::unique_ptr<CompilerInfoData> data)
    : CompilerInfo(std::move(data)) {
  LOG_IF(DFATAL, !data_->has_java())
      << "No java extension data was found in CompilerInfoData.";
}

JavacCompilerInfo::JavacCompilerInfo(std::unique_ptr<CompilerInfoData> data)
    : CompilerInfo(std::move(data)) {
  LOG_IF(DFATAL, !data_->has_javac())
      << "No javac extension data was found in CompilerInfoData.";
}

}  // namespace devtools_goma
