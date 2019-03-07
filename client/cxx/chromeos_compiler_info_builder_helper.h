// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_CHROMEOS_COMPILER_INFO_BUILDER_HELPER_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_CHROMEOS_COMPILER_INFO_BUILDER_HELPER_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "prototmp/compiler_info_data.pb.h"

using std::string;

namespace devtools_goma {

class ChromeOSCompilerInfoBuilderHelper {
 public:
  // Returns true if compiler looks like chromeos simple chrome toolcahin.
  static bool IsSimpleChromeClangCommand(absl::string_view local_compiler_path,
                                         absl::string_view real_compiler_path);

  // Collects simple chrome toolchain resources for Arbitrary Toolchain Support.
  static bool CollectSimpleChromeClangResources(
      const string& cwd,
      absl::string_view local_compiler_path,
      absl::string_view real_compiler_path,
      std::vector<string>* resource_paths);

  // Estimates major version from chromeos simple chrome toolchain.
  // Here, assuming real compiler is like `clang-<VERSION>.elf`.
  // Returns true if succeeded, false otherwise.
  static bool EstimateClangMajorVersion(absl::string_view real_compiler_path,
                                        int* version);

  // Returns true if the current environment is chroot env, and
  // local_compiler_path indicates a system clang in the chroot env.
  static bool IsClangInChrootEnv(absl::string_view local_compiler_path);
  // Collects clang resources in chromeos chroot env.
  static bool CollectChrootClangResources(absl::string_view local_compiler_path,
                                          absl::string_view real_compiler_path,
                                          std::vector<string>* resource_paths);

  static void SetAdditionalFlags(
      absl::string_view local_compiler_path,
      google::protobuf::RepeatedPtrField<std::string>* additional_flags);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_CHROMEOS_COMPILER_INFO_BUILDER_HELPER_H_
