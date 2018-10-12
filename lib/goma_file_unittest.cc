// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/goma_file.h"

#include "base/compiler_specific.h"
#include "gtest/gtest.h"
using std::string;

TEST(StringOutput, EmptyContent) {
  string buf;
  std::unique_ptr<devtools_goma::FileServiceClient::Output> output =
      devtools_goma::FileServiceClient::StringOutput("test", &buf);
  EXPECT_TRUE(output->IsValid());
  string content;
  EXPECT_TRUE(output->WriteAt(0, content));
  EXPECT_TRUE(output->Close());
  EXPECT_EQ(buf, content);
}
