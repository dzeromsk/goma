// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/file_data_output.h"

#include <memory>

#include "gtest/gtest.h"

namespace devtools_goma {

TEST(StringOutput, EmptyContent) {
  string buf;
  std::unique_ptr<FileDataOutput> output =
      FileDataOutput::NewStringOutput("test", &buf);
  EXPECT_TRUE(output->IsValid());
  string content;
  EXPECT_TRUE(output->WriteAt(0, content));
  EXPECT_TRUE(output->Close());
  EXPECT_EQ(buf, content);
}

}  // namespace devtools_goma
