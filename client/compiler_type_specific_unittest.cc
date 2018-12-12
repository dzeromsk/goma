// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compiler_type_specific.h"

#include <string>
#include <vector>

#include "compiler_type_specific_collection.h"
#include "gcc_flags.h"
#include "gtest/gtest.h"
#include "vc_flags.h"

namespace devtools_goma {

TEST(CompilerTypeSpecificTest, SupportsDepsCache) {
  CompilerTypeSpecificCollection collection;

  // gcc (compile)
  {
    const std::vector<string> args{
        "gcc",
        "-c",
        "main.c",
    };
    GCCFlags flags(args, "/tmp");

    EXPECT_TRUE(collection.Get(flags.type())->SupportsDepsCache(flags));
  }

  // clang (compile)
  {
    const std::vector<string> args{
        "clang",
        "-c",
        "main.c",
    };
    GCCFlags flags(args, "/tmp");

    EXPECT_TRUE(collection.Get(flags.type())->SupportsDepsCache(flags));
  }

  // gcc (link)
  {
    const std::vector<string> args{
        "gcc",
        "main.c",
    };
    GCCFlags flags(args, "/tmp");

    EXPECT_FALSE(collection.Get(flags.type())->SupportsDepsCache(flags));
  }

  // clang (link)
  {
    const std::vector<string> args{
        "clang",
        "main.c",
    };
    GCCFlags flags(args, "/tmp");

    EXPECT_FALSE(collection.Get(flags.type())->SupportsDepsCache(flags));
  }

  // vc (compile only; link is not supported)
  {
    const std::vector<string> args{
        "cl.exe",
        "/c",
        "main.c",
    };
    VCFlags flags(args, "/tmp");

    EXPECT_TRUE(collection.Get(flags.type())->SupportsDepsCache(flags));
  }
}

}  // namespace devtools_goma
