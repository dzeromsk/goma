// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_GOMA_DATA_UTIL_H_
#define DEVTOOLS_GOMA_LIB_GOMA_DATA_UTIL_H_

#include <string>

using std::string;

namespace devtools_goma {

class ExecReq;
class ExecResp;
class FileBlob;

// Returns true if subprograms in ExecReq and ExecResp are the same.
bool IsSameSubprograms(const ExecReq& req, const ExecResp& resp);

// Checks that fields of |blob| are set to valid values. See the comments of
// FileBlob's definition to see the rules of validity.
bool IsValidFileBlob(const FileBlob& blob);

// Compute a unique hash key of from the contents of |blob|.
string ComputeFileBlobHashKey(const FileBlob& blob);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_GOMA_DATA_UTIL_H_
