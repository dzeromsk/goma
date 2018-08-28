// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_BUILDER_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_BUILDER_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "compiler_flags.h"
#include "compiler_specific.h"
#include "sha256_hash_cache.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

// CompilerInfoBuilder provides methods to construct CompilerInfoData.
// For each compiler type, derive own CompilerInfoBuilder from this class.
class CompilerInfoBuilder {
 public:
  virtual ~CompilerInfoBuilder() = default;

  // Creates new CompilerInfoData* from compiler outputs.
  // if found is true and error_message in it is empty,
  // it successfully gets compiler info.
  // if found is true and error_message in it is not empty,
  // it finds local compiler but failed to get some information, such as
  // system include paths.
  // if found is false if it fails to find local compiler.
  // Caller should take ownership of returned CompilerInfoData.
  std::unique_ptr<CompilerInfoData> FillFromCompilerOutputs(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const std::vector<string>& compiler_info_envs);

  virtual void SetCompilerPath(const CompilerFlags& flags,
                               const string& local_compiler_path,
                               const std::vector<string>& compiler_info_envs,
                               CompilerInfoData* data) const;

  virtual void SetTypeSpecificCompilerInfo(
      const CompilerFlags& flags,
      const string& local_compiler_path,
      const string& abs_local_compiler_path,
      const std::vector<string>& compiler_info_envs,
      CompilerInfoData* data) const = 0;

  // Returns compiler name to be used in ExecReq's CompilerSpec.
  // If it fails to identify the compiler name, it returns empty string.
  virtual string GetCompilerName(const CompilerInfoData& data) const;

  // Adds error message to CompilerInfo. When |failed_at| is not 0,
  // it's also updated.
  static void AddErrorMessage(const std::string& message,
                              CompilerInfoData* compiler_info);
  // Overrides the current error message.
  // if |message| is not empty, |failed_at| must be valid.
  static void OverrideError(const std::string& message,
                            absl::optional<absl::Time> failed_at,
                            CompilerInfoData* compiler_info);

  static bool ResourceInfoFromPath(const string& cwd,
                                   const string& path,
                                   CompilerInfoData::ResourceType type,
                                   CompilerInfoData::ResourceInfo* r);

 protected:
  CompilerInfoBuilder() = default;

 private:
  SHA256HashCache hash_cache_;

  DISALLOW_COPY_AND_ASSIGN(CompilerInfoBuilder);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_BUILDER_H_
