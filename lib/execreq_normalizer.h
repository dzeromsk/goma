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
