// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "java/java_compiler_info_builder.h"

#include "gtest/gtest.h"

namespace devtools_goma {

TEST(JavacCompilerInfoBuilderTest, GetJavacVersion) {
  static const char kVersionInfo[] = "javac 1.6.0_43\n";

  string version;
  JavacCompilerInfoBuilder::ParseJavacVersion(kVersionInfo, &version);
  EXPECT_EQ("1.6.0_43", version);
}

}  // namespace devtools_goma
