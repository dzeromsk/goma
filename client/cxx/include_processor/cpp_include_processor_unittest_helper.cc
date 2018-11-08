// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_include_processor_unittest_helper.h"

#include <algorithm>
#include <vector>

#include "absl/strings/str_join.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"

namespace devtools_goma {

void CompareFiles(const std::set<string>& expected_files,
                  const std::set<string>& actual_files,
                  const std::set<string>& allowed_extra_files) {
  std::vector<string> matched_files;
  std::vector<string> missing_files;
  std::vector<string> extra_files;
  std::vector<string> nonallowed_extra_files;

  std::set_intersection(expected_files.begin(), expected_files.end(),
                        actual_files.begin(), actual_files.end(),
                        back_inserter(matched_files));
  std::set_difference(expected_files.begin(), expected_files.end(),
                      matched_files.begin(), matched_files.end(),
                      back_inserter(missing_files));
  std::set_difference(actual_files.begin(), actual_files.end(),
                      matched_files.begin(), matched_files.end(),
                      back_inserter(extra_files));
  std::set_difference(extra_files.begin(), extra_files.end(),
                      allowed_extra_files.begin(), allowed_extra_files.end(),
                      back_inserter(nonallowed_extra_files));

  LOG(INFO) << "matched:" << matched_files.size()
            << " extra:" << extra_files.size()
            << " nonallowed extra: " << nonallowed_extra_files.size()
            << " missing:" << missing_files.size();
  LOG_IF(INFO, !extra_files.empty())
      << "extra files: " << absl::StrJoin(extra_files, ", ");
  LOG_IF(INFO, !nonallowed_extra_files.empty())
      << "nonallowed extra files: "
      << absl::StrJoin(nonallowed_extra_files, ", ");
  LOG_IF(INFO, !missing_files.empty())
      << "missing files: " << absl::StrJoin(missing_files, ", ");

  EXPECT_EQ(0U, missing_files.size()) << missing_files;

#ifdef __MACH__
  // See: b/26573474
  LOG_IF(WARNING, !nonallowed_extra_files.empty()) << nonallowed_extra_files;
#else
  EXPECT_TRUE(nonallowed_extra_files.empty()) << nonallowed_extra_files;
#endif
}

}  // namespace devtools_goma
