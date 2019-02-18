// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base64.h"

#include <string>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"
#include "basictypes.h"

namespace devtools_goma {

static const char* kEncodeURL =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";

std::string Base64UrlEncode(absl::string_view str, bool padding) {
  if (str.empty()) {
    return "";
  }
  std::string dst;
  int si = 0;
  int n = (static_cast<int>(str.size()) / 3) * 3;
  while (si < n) {
    int val = (((str[si+0])&0xFFU) << 16) | ((str[si+1]&0xFFU) << 8) |
        (str[si+2]&0xFFU);

    dst += kEncodeURL[(val >> 18) & 0x3F];
    dst += kEncodeURL[(val >> 12) & 0x3F];
    dst += kEncodeURL[(val >>  6) & 0x3F];
    dst += kEncodeURL[val & 0x3F];

    si += 3;
  }
  int remain = static_cast<int>(str.size()) - si;
  int val = 0;
  switch (remain) {
    case 2:
      val |= (str[si+1] & 0xFFU) << 8;
      ABSL_FALLTHROUGH_INTENDED;
    case 1:
      val |= (str[si+0] & 0xFFU) << 16;
      break;
    case 0:
      return dst;
  }
  dst += kEncodeURL[(val >> 18) & 0x3F];
  dst += kEncodeURL[(val >> 12) & 0x3F];
  switch (remain) {
    case 1:
      if (padding) {
        dst += '=';
        dst += '=';
      }
      break;
    case 2:
      dst += kEncodeURL[(val >> 6) & 0x3F];
      if (padding) {
        dst += '=';
      }
      break;
  }
  return dst;
}

}  // namespace devtools_goma
