// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_version.h"

#include "gtest/gtest.h"

TEST(MacVersion, MacOSXMinorVersion) {
  // If MacOSXMinorVersion successfully worked,
  // it should return value greater than or equal to 2.
  EXPECT_GE(devtools_goma::MacOSXMinorVersion(), 2);
}
