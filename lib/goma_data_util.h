// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_GOMA_DATA_UTIL_H_
#define DEVTOOLS_GOMA_LIB_GOMA_DATA_UTIL_H_

namespace devtools_goma {

class ExecReq;
class ExecResp;

// Returns true if subprograms in ExecReq and ExecResp are the same.
bool IsSameSubprograms(const ExecReq& req, const ExecResp& resp);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_GOMA_DATA_UTIL_H_
