// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "ioutil.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "unittest_util.h"

using std::string;

namespace devtools_goma {

#if GTEST_HAS_DEATH_TEST
TEST(IoutilTest, WriteStringToFileOrDieCrash) {
#ifndef _WIN32
  string not_exists = "/tmp/you_may_not_have_this_dir/foo/bar/baz";
  EXPECT_DEATH(WriteStringToFileOrDie("fuga", not_exists, 0666),
               "No such file");
#else
  string not_exists = "K:\\tmp\\you_may_not_have_this_dir\\foo\\bar\\baz";
  EXPECT_DEATH(WriteStringToFileOrDie("fuga", not_exists, 0666), "");
#endif
}
#endif  // GTEST_HAS_DEATH_TEST

}  // namespace devtools_goma
