// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_EXECREQ_VERIFIER_H_
#define DEVTOOLS_GOMA_LIB_EXECREQ_VERIFIER_H_

#include "base/compiler_specific.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

bool VerifyExecReq(const ExecReq& req);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_EXECREQ_VERIFIER_H_
