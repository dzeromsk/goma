// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_BASE64_H_
#define DEVTOOLS_GOMA_CLIENT_BASE64_H_

#include <string>

#include "absl/strings/string_view.h"

namespace devtools_goma {

// Base64UrlEncode encodes str with base64 url encoding.
std::string Base64UrlEncode(absl::string_view str, bool padding);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_BASE64_H_
