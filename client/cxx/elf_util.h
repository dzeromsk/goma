// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_ELF_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_ELF_UTIL_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest_prod.h"

namespace devtools_goma {

class ElfDepParser {
 public:
  ElfDepParser(std::string cwd,
               std::vector<std::string> default_search_paths,
               bool ignore_rpath)
      : cwd_(std::move(cwd)),
        default_search_paths_(std::move(default_search_paths)),
        ignore_rpath_(ignore_rpath) {}

  virtual ~ElfDepParser() {}

  ElfDepParser(ElfDepParser&&) = delete;
  ElfDepParser(const ElfDepParser&) = delete;
  ElfDepParser& operator=(const ElfDepParser&) = delete;
  ElfDepParser& operator=(ElfDepParser&&) = delete;

  // List up all library dependencies for |cmd_or_lib| and stored to |deps|.
  // Stored paths will be a relative paths from |cwd_| if there is no
  // absolute paths in RPATH.
  // The function returns true on success.
  bool GetDeps(const absl::string_view cmd_or_lib,
               absl::flat_hash_set<std::string>* deps);

 private:
  FRIEND_TEST(ElfDepParserTest, ParseReadElf);

  // Returns relative library path name if succeeds.
  // Otherwise, empty string will be returned.
  std::string FindLib(const absl::string_view lib_filename,
                      const absl::string_view origin,
                      const std::vector<absl::string_view>& search_paths) const;

  // ParseReadElf result.
  // Note: |libs| and |rpath| will have pointers to |content|.
  static bool ParseReadElf(absl::string_view content,
                           std::vector<absl::string_view>* libs,
                           std::vector<absl::string_view>* rpaths);

  const std::string cwd_;
  const std::vector<std::string> default_search_paths_;
  const bool ignore_rpath_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_ELF_UTIL_H_
