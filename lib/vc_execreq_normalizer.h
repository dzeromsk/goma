// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_LIB_VC_EXECREQ_NORMALIZER_H_
#define DEVTOOLS_GOMA_LIB_VC_EXECREQ_NORMALIZER_H_


#include "execreq_normalizer.h"
using std::string;

namespace devtools_goma {

class VCExecReqNormalizer : public ConfigurableExecReqNormalizer {
 protected:
  Config Configure(int id,
                   const std::vector<string>& args,
                   bool normalize_include_path,
                   bool is_linking,
                   const std::vector<string>& normalize_weak_relative_for_arg,
                   const std::map<string, string>& debug_prefix_map,
                   const ExecReq* req) const override;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_VC_EXECREQ_NORMALIZER_H_
