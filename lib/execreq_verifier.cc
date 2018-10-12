// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/execreq_verifier.h"

namespace devtools_goma {

bool VerifyExecReq(const ExecReq& req) {
  if (!req.IsInitialized()) {
    return false;
  }
  const CommandSpec& spec = req.command_spec();
  if (!spec.has_name() || !spec.has_version() || !spec.has_target()) {
    return false;
  }
  if (req.arg_size() == 0) {
    return false;
  }
  if (!req.has_cwd()) {
    return false;
  }
  for (const auto& input : req.input()) {
    if (!input.has_filename()) {
      return false;
    }
  }
  return true;
}

}  // namespace devtools_goma
