// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "elf_parser.h"

#include <cstdlib>
#include <iostream>

#include "glog/logging.h"
#include "goma_init.h"

using devtools_goma::ElfParser;

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cout << "Usage: " << argv[0] << " <filename>" << std::endl;
    exit(EXIT_FAILURE);
  }
  google::InitGoogleLogging(argv[0]);

  std::unique_ptr<ElfParser> elf = ElfParser::NewElfParser(argv[1]);
  CHECK(elf != nullptr);
  CHECK(elf->valid());
  std::vector<string> needed, rpath;
  if (!elf->ReadDynamicNeededAndRpath(&needed, &rpath)) {
    LOG(FATAL) << "ReadDynamicNeededAndRpath";
  }
  for (const auto& it : needed) {
    std::cout << "NEEDED:" << it << std::endl;
  }
  for (const auto& it : rpath) {
    std::cout << "RPATH:" << it << std::endl;
  }
  exit(0);
}
