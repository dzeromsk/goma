// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_INFO_H_
#define DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_INFO_H_

#include <memory>

#include "compiler_info.h"
#include "glog/logging.h"

namespace devtools_goma {

class JavaCompilerInfo : public CompilerInfo {
 public:
  explicit JavaCompilerInfo(std::unique_ptr<CompilerInfoData> data);
  CompilerInfoType type() const override { return CompilerInfoType::Java; }
};

inline const JavaCompilerInfo& ToJavaCompilerInfo(
    const CompilerInfo& compiler_info) {
  DCHECK_EQ(CompilerInfoType::Java, compiler_info.type());
  return static_cast<const JavaCompilerInfo&>(compiler_info);
}

class JavacCompilerInfo : public CompilerInfo {
 public:
  explicit JavacCompilerInfo(std::unique_ptr<CompilerInfoData> data);
  CompilerInfoType type() const override { return CompilerInfoType::Javac; }
};

inline const JavacCompilerInfo& ToJavacCompilerInfo(
    const CompilerInfo& compiler_info) {
  DCHECK_EQ(CompilerInfoType::Javac, compiler_info.type());
  return static_cast<const JavacCompilerInfo&>(compiler_info);
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_JAVA_JAVA_COMPILER_INFO_H_
