// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_FILE_HELPER_H_
#define DEVTOOLS_GOMA_LIB_FILE_HELPER_H_

#include <string>


#include "string_piece.h"
using std::string;

namespace devtools_goma {

bool ReadFileToString(absl::string_view file_name, string* OUTPUT);
bool WriteStringToFile(absl::string_view data, absl::string_view file_name);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_FILE_HELPER_H_
