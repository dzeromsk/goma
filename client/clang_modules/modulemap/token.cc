// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "token.h"

namespace devtools_goma {
namespace modulemap {

std::ostream& operator<<(std::ostream& os, const Token& token) {
  switch (token.type()) {
    case Token::Type::STRING:
      return os << "[\"" << token.value() << "\"]";
    case Token::Type::IDENT:
      return os << '[' << token.value() << ']';
    case Token::Type::INTEGER:
      return os << "[<INT:" << token.value() << ">]";
    case Token::Type::PUNC:
      return os << "[<PUNC:" << token.value() << ">]";
    case Token::Type::END:
      return os << "[<END>]";
    case Token::Type::INVALID:
      return os << "[<INVALID>]";
  }

  return os << "[<UNKNOWN>]";
}

}  // namespace modulemap
}  // namespace devtools_goma
