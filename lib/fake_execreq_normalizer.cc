// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fake_execreq_normalizer.h"

#include "glog/logging.h"
using std::string;

namespace devtools_goma {

ConfigurableExecReqNormalizer::Config FakeExecReqNormalizer::Configure(
    int id,
    const std::vector<string>& args,
    bool normalize_include_path,
    bool is_linking,
    const std::vector<string>& normalize_weak_relative_for_arg,
    const std::map<string, string>& debug_prefix_map,
    const ExecReq* req) const {
  return Config::AsIs();
}

}  // namespace devtools_goma
