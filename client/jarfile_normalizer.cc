// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <string>

#include "glog/logging.h"
#include "goma_init.h"
#include "jarfile_reader.h"
#include "scoped_fd.h"

namespace devtools_goma {

class JarFileNormalizer {
 public:
  JarFileNormalizer(char *input, char *output):
    input_(input), output_(output) {}

  bool DoNormalize() {
    JarFileReader reader(input_);
    if (!reader.valid()) {
      std::cerr << "input file: " << input_ << " is invalid. "
                << "not exist or not a valid jar file."
                << std::endl;
      return false;
    }
    ScopedFd out(devtools_goma::ScopedFd::CreateExclusive(output_, 0644));
    if (!out.valid()) {
      std::cerr << "output file: " << output_ << " cannot be opened."
                << " file exists or permission denied."
                << std::endl;
      return false;
    }

    for (;;) {
      char buf[4096];
      ssize_t read_bytes = reader.Read(buf, sizeof(buf));
      if (read_bytes < 0) {
        std::cerr << "failed to read." << std::endl;
        return false;
      }
      CHECK(out.Write(buf, read_bytes) == read_bytes);
      if (read_bytes < sizeof(buf)) {
        break;
      }
    }
    return out.Close();
  }

 private:
  const std::string input_;
  const std::string output_;
};

}  // namespace devtools_goma

int main(int argc, char *argv[], const char** envp) {
  devtools_goma::Init(argc, argv, envp);
  devtools_goma::InitLogging(argv[0]);

  if (argc != 3) {
    std::cerr << argv[0] << " [source jar file] [destination jar file]"
              << std::endl;
    std::cerr << "e.g.: " << argv[0] << " test/Basic.jar /tmp/normalized.jar"
              << std::endl;
    std::exit(EXIT_FAILURE);
  }
  devtools_goma::JarFileNormalizer normalizer(argv[1], argv[2]);
  if (!normalizer.DoNormalize()) {
    std::cerr << "Failed to normalize." << std::endl;
    std::exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}
