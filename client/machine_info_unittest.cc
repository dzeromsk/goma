// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "machine_info.h"

#include <gtest/gtest.h>

namespace devtools_goma {

TEST(MachineInfoTest, Smoke) {
  EXPECT_NE(0, GetNumCPUs());
  EXPECT_NE(0, GetSystemTotalMemory());
  EXPECT_NE(0, GetConsumingMemoryOfCurrentProcess());
}

}  // namespace devtools_goma
