// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro_set.h"

#include "absl/memory/memory.h"
#include "cpp_macro.h"
#include "gtest/gtest.h"

namespace devtools_goma {

TEST(CppParserTest, MacroSet) {
  auto m0(
      absl::make_unique<Macro>("a", Macro::OBJ, ArrayTokenList(), 0, false));
  auto m1(
      absl::make_unique<Macro>("a", Macro::OBJ, ArrayTokenList(), 0, false));
  auto m2(
      absl::make_unique<Macro>("a", Macro::OBJ, ArrayTokenList(), 0, false));
  auto m3(
      absl::make_unique<Macro>("a", Macro::OBJ, ArrayTokenList(), 0, false));

  MacroSet a, b, c;
  EXPECT_TRUE(a.empty());
  a.Set(m1.get());
  a.Set(m2.get());
  b.Set(m3.get());
  EXPECT_FALSE(a.empty());
  EXPECT_FALSE(b.empty());
  EXPECT_TRUE(a.Has(m1.get()));
  EXPECT_FALSE(a.Has(m3.get()));
  EXPECT_FALSE(b.Has(m1.get()));
  EXPECT_TRUE(b.Has(m3.get()));
  a.Union(b);
  EXPECT_FALSE(a.Has(m0.get()));
  EXPECT_TRUE(a.Has(m1.get()));
  EXPECT_TRUE(a.Has(m3.get()));
}

}  // namespace devtools_goma
