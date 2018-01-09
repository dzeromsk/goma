// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_BASE_SPLIT_H_
#define DEVTOOLS_GOMA_BASE_SPLIT_H_

#include <string>
#include <vector>

using std::string;

void SplitStringUsing(const string& full, const char* delim,
                      std::vector<string>* res);

namespace strings {

// Support only Example 1 of new Split API.
// TODO: full support of new Split API.
std::vector<string> Split(const string& full, const string& delim);
inline std::vector<string> Split(const string& full, char c) {
  return Split(full, string(1, c));
}

}  // namespace strings

#endif  // DEVTOOLS_GOMA_BASE_SPLIT_H_
