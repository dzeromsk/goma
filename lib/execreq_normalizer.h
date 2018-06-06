// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_EXECREQ_NORMALIZER_H_
#define DEVTOOLS_GOMA_LIB_EXECREQ_NORMALIZER_H_


#include <map>
#include <string>
#include <vector>

#include "compiler_specific.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
using std::string;

namespace devtools_goma {

// ExecReqNormalizer is an interface for ExecReq normalization.
class ExecReqNormalizer {
 public:
  virtual ~ExecReqNormalizer() = default;

  virtual void Normalize(
      int id,
      const std::vector<string>& args,
      bool normalize_include_path,
      bool is_linking,
      const std::vector<string>& normalize_weak_relative_for_arg,
      const std::map<string, string>& debug_prefix_map,
      ExecReq* req) const = 0;
};

// ConfigurableExecReqNormalizer provides configurable exec req normalizer.
class ConfigurableExecReqNormalizer : public ExecReqNormalizer {
 public:
  void Normalize(int id,
                 const std::vector<string>& args,
                 bool normalize_include_path,
                 bool is_linking,
                 const std::vector<string>& normalize_weak_relative_for_arg,
                 const std::map<string, string>& debug_prefix_map,
                 ExecReq* req) const override;

 protected:
  static const int kOmit = 0;
  static const int kNormalizeWithCwd = 1 << 0;
  static const int kNormalizeWithDebugPrefixMap = 1 << 1;
  static const int kPreserveI = 1 << 2;
  static const int kAsIs = 1 << 3;

  struct Config {
    int keep_cwd = kAsIs;
    int keep_args = kAsIs;
    int keep_pathnames_in_input = kAsIs;
    int keep_system_include_dirs = kAsIs;
  };

  // Each compiler-specific ExecReqNormalizer will configure this.
  virtual Config Configure(
      int id,
      const std::vector<string>& args,
      bool normalize_include_path,
      bool is_linking,
      const std::vector<string>& normalize_weak_relative_for_arg,
      const std::map<string, string>& debug_prefix_map,
      const ExecReq* req) const;

 private:
  void NormalizeExecReqSystemIncludeDirs(
      int keep_system_include_dirs,
      const std::map<string, string>& debug_prefix_map,
      const string& debug_prefix_map_signature,
      ExecReq* req) const;
  void NormalizeExecReqArgs(
      int keep_args,
      const std::vector<string>& args,
      const std::vector<string>& normalize_weak_relative_for_arg,
      const std::map<string, string>& debug_prefix_map,
      const string& debug_prefix_map_signature,
      ExecReq* req) const;

  // This method needs cwd and filename in ExecReq_Input.
  // So, do before processing keep_pathnames and keep_cwd.
  void NormalizeExecReqInputOrderForCacheKey(ExecReq* req) const;

  void NormalizeExecReqPathnamesInInput(
      int keep_pathnames_in_input,
      const std::map<string, string>& debug_prefix_map,
      const string& debug_prefix_map_signature,
      ExecReq* req) const;
  void NormalizeExecReqCwd(int keep_cwd,
                           const std::map<string, string>& debug_prefix_map,
                           const string& debug_prefix_map_signature,
                           ExecReq* req) const;

  void NormalizeExecReqSubprograms(ExecReq* req) const;
  void NormalizeExecReqEnvs(ExecReq* req) const;
};

// Normalize ExecReq for cache key. |req| will be modified.
// |id| is used for logging purpose.
//
// How to disable normalization?
//   system_include_paths: set |normalize_include_path| false.
//   args: make |normalize_weak_relative_for_arg| empty.
//   normalization using fdebug_prefix_map: make |debug_prefix_map| empty.
void NormalizeExecReqForCacheKey(
    int id,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    ExecReq* req);

bool RewritePathWithDebugPrefixMap(
    const std::map<string, string>& debug_prefix_map,
    string* path);

bool HasAmbiguityInDebugPrefixMap(
    const std::map<string, string>& debug_prefix_map);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_EXECREQ_NORMALIZER_H_
