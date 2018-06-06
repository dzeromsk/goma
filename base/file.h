// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_BASE_FILE_H_
#define DEVTOOLS_GOMA_BASE_FILE_H_

#include <string>

#include "absl/strings/string_view.h"
#include "status.h"

namespace File {

bool Copy(const char* from, const char* to, bool overwrite);

}  // namespace File

#endif  // DEVTOOLS_GOMA_BASE_FILE_H_
