// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_info_state.h"

#include "gtest/gtest.h"

namespace devtools_goma {

class ScopedCompilerInfoStateTest : public testing::Test {
 protected:
  void FillFromCompilerOutputs(ScopedCompilerInfoState* cis) {
    std::unique_ptr<CompilerInfoData> data(new CompilerInfoData);
    data->set_found(true);
    data->mutable_cxx();
    cis->reset(new CompilerInfoState(std::move(data)));
  }
};

TEST_F(ScopedCompilerInfoStateTest, reset) {
  ScopedCompilerInfoState cis;
  FillFromCompilerOutputs(&cis);
  EXPECT_TRUE(cis.get() != nullptr);
  EXPECT_EQ(1, cis.get()->refcnt());

  cis.reset(cis.get());
  EXPECT_TRUE(cis.get() != nullptr);
  EXPECT_EQ(1, cis.get()->refcnt());
}

}  // namespace devtools_goma
