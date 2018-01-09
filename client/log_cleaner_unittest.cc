// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "log_cleaner.h"

#include <gtest/gtest.h>

namespace devtools_goma {

class LogCleanerTest : public testing::Test {
 public:
  LogCleanerTest() {
    log_cleaner_.AddLogBasename("compiler_proxy");
    log_cleaner_.AddLogBasename("compiler_proxy-subproc");
    log_cleaner_.AddLogBasename("gcc");
    log_cleaner_.AddLogBasename("g++");
  }

  bool IsMyLogFile(const string& name) {
    return log_cleaner_.IsMyLogFile(name);
  }

 private:
  LogCleaner log_cleaner_;
};

TEST_F(LogCleanerTest, IsMyLogFile) {
  EXPECT_TRUE(IsMyLogFile(
      "compiler_proxy.example.com.goma.log.INFO."
      "20111017-165526.12857"));
  EXPECT_TRUE(IsMyLogFile(
      "compiler_proxy.example.com.goma.log.WARNING."
      "20111017-165526.12857"));
  EXPECT_TRUE(IsMyLogFile(
      "compiler_proxy.example.com.goma.log.ERROR."
      "20111017-165526.12857"));
  EXPECT_TRUE(IsMyLogFile(
      "compiler_proxy-subproc.example.com.goma.log.INFO."
      "20111017-165526.12857"));
  EXPECT_TRUE(IsMyLogFile(
      "gcc.example.com.goma.log.INFO."
      "20111017-165526.12857"));
  EXPECT_TRUE(IsMyLogFile(
      "g++.example.com.goma.log.INFO."
      "20111017-165526.12857"));
  EXPECT_FALSE(IsMyLogFile("g++.log"));
}

}  // namespace devtools_goma
