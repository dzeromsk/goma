// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "ioutil.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  std::string input(reinterpret_cast<const char*>(data), size);

  int http_status_code;
  size_t offset;
  size_t content_length;
  bool is_chunked;
  devtools_goma::ParseHttpResponse(
      input, &http_status_code, &offset, &content_length, &is_chunked);

  return 0;
}
