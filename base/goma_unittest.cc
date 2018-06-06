// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// Modified version of gtest_main.cc so that Winsock can be initialized

#include <iostream>

#include "gtest/gtest.h"

#ifdef _WIN32
#include "socket_helper_win.h"
#endif

int main(int argc, char **argv) {
#ifdef _WIN32
  WinsockHelper wsa;
#endif
  std::cout << "Running main() from gtest_main.cc\n";

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
