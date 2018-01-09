// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// BEGIN GOOGLE-INTERNAL
// strings/stringpiece_utils emulation layer.
// END GOOGLE-INTENRAL

#include "string_piece_utils.h"

#include <cstring>
#include <sstream>

namespace strings {

namespace internal {

std::string StrCatImpl(std::initializer_list<StringPiece> pieces) {
  std::stringstream ss;
  for (const auto& s : pieces) {
    ss << s;
  }
  return ss.str();
}

}  // namespace internal

bool StartsWith(StringPiece s, StringPiece x) {
  return s.size() >= x.size() &&
      std::memcmp(s.data(), x.data(), x.size()) == 0;
}

bool EndsWith(StringPiece s, StringPiece x) {
  return s.size() >= x.size() &&
      std::memcmp(s.data() + (s.size() - x.size()), x.data(), x.size()) == 0;
}

}  // namespace strings
