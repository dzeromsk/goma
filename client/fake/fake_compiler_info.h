// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_COMPILER_INFO_H_
#define DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_COMPILER_INFO_H_

#include <memory>

#include "compiler_info.h"
#include "glog/logging.h"

namespace devtools_goma {

class FakeCompilerInfo : public CompilerInfo {
 public:
  explicit FakeCompilerInfo(std::unique_ptr<CompilerInfoData> data);
  CompilerInfoType type() const override { return CompilerInfoType::Fake; }
};

inline const FakeCompilerInfo& ToFakeCompilerInfo(
    const CompilerInfo& compiler_info) {
  DCHECK_EQ(CompilerInfoType::Fake, compiler_info.type());
  return static_cast<const FakeCompilerInfo&>(compiler_info);
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FAKE_FAKE_COMPILER_INFO_H_
