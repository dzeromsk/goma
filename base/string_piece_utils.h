// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_BASE_STRING_PIECE_UTILS_H_
#define DEVTOOLS_GOMA_BASE_STRING_PIECE_UTILS_H_

#include <initializer_list>

#include "string_piece.h"

// TODO: Replace strings -> absl
namespace strings {

namespace internal {
std::string StrCatImpl(std::initializer_list<StringPiece> pieces);
}

// Returns whether s begins with x.
bool StartsWith(StringPiece s, StringPiece x);

// Returns whether s ends with x.
bool EndsWith(StringPiece s, StringPiece x);

template<typename... Strs>
inline std::string StrCat(const Strs&... pieces) {
  return internal::StrCatImpl({pieces...});
}

}  // namespace strings

#endif  // DEVTOOLS_GOMA_BASE_STRING_PIECE_UTILS_H_
