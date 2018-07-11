// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_macro.h"

#include "autolock_timer.h"

namespace devtools_goma {

// static
bool Macro::IsParenBalanced(const ArrayTokenList& tokens) {
  int level = 0;
  for (const auto& t : tokens) {
    if (t.IsPuncChar('(')) {
      ++level;
    } else if (t.IsPuncChar(')')) {
      --level;
      if (level < 0) {
        return false;
      }
    }
  }
  return level == 0;
}

string Macro::DebugString(CppParser* parser) const {
  string str;
  str.reserve(64);
  str.append("Macro[");
  str.append(name);
  switch (type) {
    case OBJ:
      str.append("(OBJ)]");
      break;
    case FUNC:
      str.append("(FUNC, args:");
      str.append(std::to_string(num_args));
      if (is_vararg) {
        str.append(", vararg");
      }
      str.append(")]");
      break;
    case CBK:
      str.append("(CALLBACK)]");
      break;
    case CBK_FUNC:
      str.append("(CALLBACK_FUNC)]");
      break;
  }
  str.append(" => ");
  if (callback) {
    str.append((parser->*callback)().DebugString());
  } else {
    for (const auto& iter : replacement) {
      str.append(iter.DebugString());
    }
  }
  return str;
}

}  // namespace devtools_goma
