// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "goma_data_util.h"

#include <algorithm>
#include <string>
#include <vector>

#include "prototmp/goma_data.pb.h"
using std::string;

namespace devtools_goma {

bool IsSameSubprograms(const ExecReq& req, const ExecResp& resp) {
  if (req.subprogram_size() != resp.result().subprogram_size()) {
    return false;
  }

  std::vector<string> req_hashes;
  for (const auto& subprogram : req.subprogram()) {
    req_hashes.push_back(subprogram.binary_hash());
  }
  std::vector<string> resp_hashes;
  for (const auto& subprogram : resp.result().subprogram()) {
    resp_hashes.push_back(subprogram.binary_hash());
  }
  std::sort(req_hashes.begin(), req_hashes.end());
  std::sort(resp_hashes.begin(), resp_hashes.end());
  return req_hashes == resp_hashes;
}

}  // namespace devtools_goma
