// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fake_flags.h"

#include "glog/logging.h"
#include "gtest/gtest.h"
using std::string;

namespace devtools_goma {

TEST(FakeFlagsTest, Basic) {
  const std::vector<string> args{
      "fake", "foo.fake", "bar.fake",
  };
  const string cwd = ".";

  FakeFlags flags(args, cwd);
  EXPECT_TRUE(flags.is_successful());
  EXPECT_EQ((std::vector<string>{"foo.fake", "bar.fake"}),
            flags.input_filenames());
  EXPECT_EQ((std::vector<string>{"foo.out", "bar.out"}), flags.output_files());
}

TEST(FakeFlagsTest, IsFakeCommand) {
  EXPECT_TRUE(FakeFlags::IsFakeCommand("fake"));
  EXPECT_TRUE(FakeFlags::IsFakeCommand("/usr/bin/fake"));
  EXPECT_TRUE(FakeFlags::IsFakeCommand("fake.exe"));

  EXPECT_FALSE(FakeFlags::IsFakeCommand("foo"));
  EXPECT_FALSE(FakeFlags::IsFakeCommand("bar"));
}

TEST(FakeFlagsTest, GetCompilerName) {
  EXPECT_EQ("fake", FakeFlags::GetCompilerName("fake"));
  EXPECT_EQ("fake", FakeFlags::GetCompilerName("fake.exe"));
}

}  // namespace devtools_goma
