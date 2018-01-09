// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "env_flags.h"

#include <string>

#include "gtest/gtest.h"

using std::string;

namespace devtools_goma {

static const int kInitialValue = 0;
static const int kAutoConfiguredValue = 72;

static int DefaultIntValueForUnittest() {
  return kAutoConfiguredValue;
}

GOMA_DEFINE_AUTOCONF_int32(INTVAL_FOR_UNITTEST,
                           DefaultIntValueForUnittest,
                           "For testing only.");

class EnvFlagsTest : public testing::Test {
  void SetUp() override {
    // When |envp| does not contain GOMA_INTVAL_FOR_UNITTEST,
    // AutoConfigureFlags() will set the auto configured value to
    // FLAGS_INTVAL_FOR_UNITTEST. However, when |envp| contains
    // GOMA_INTVAL_FOR_UNITTEST, AutoConfigureFlags() does not parse |envp| to
    // set FLAGS_INTVAL_FOR_UNITTEST, i.e. value in |envp| will be just ignored.
    // So we have to set an initial value to FLAGS_INTVAL_FOR_UNITTEST here.
    FLAGS_INTVAL_FOR_UNITTEST = kInitialValue;
  }
};

TEST_F(EnvFlagsTest, EmptyEnv) {
  const char* envp[] = {
    nullptr
  };
  AutoConfigureFlags(envp);

  EXPECT_EQ(kAutoConfiguredValue, FLAGS_INTVAL_FOR_UNITTEST);
}

TEST_F(EnvFlagsTest, EnvGivenByUser1) {
  const char* envp[] = {
    "GOMA_INTVAL_FOR_UNITTEST=0",
    nullptr
  };
  AutoConfigureFlags(envp);

  EXPECT_EQ(kInitialValue, FLAGS_INTVAL_FOR_UNITTEST);
}

TEST_F(EnvFlagsTest, EnvGivenByUser2) {
  const char* envp[] = {
    "GOMA_INTVAL_FOR_UNITTEST=1",
    nullptr
  };
  FLAGS_INTVAL_FOR_UNITTEST = 1;
  AutoConfigureFlags(envp);

  // Since AutuConfigureFlags does not parse |envp|,
  // FLAGS_INTVAL_FOR_UNITTEST should still be the same before
  // calling AutoConfigureFlags.
  EXPECT_EQ(1, FLAGS_INTVAL_FOR_UNITTEST);
}

TEST_F(EnvFlagsTest, NoGomaPrefix) {
  const char* envp[] = {
    "TEST=0",
    nullptr
  };
  AutoConfigureFlags(envp);

  EXPECT_EQ(kAutoConfiguredValue, FLAGS_INTVAL_FOR_UNITTEST);
}

TEST_F(EnvFlagsTest, VariousEnv) {
  const char* envp[] = {
    "GOMA_PRE=test",
    "GOMA_INTVAL_FOR_UNITTEST=0",
    "GOMA_POST=test",
    nullptr
  };
  AutoConfigureFlags(envp);

  EXPECT_EQ(kInitialValue, FLAGS_INTVAL_FOR_UNITTEST);
}

}  // namespace devtools_goma
