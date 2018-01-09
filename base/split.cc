// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "split.h"

#include <string>
#include <vector>

using std::string;

void SplitStringUsing(const string& full, const char* delim,
                      std::vector<string>* res) {
  *res = strings::Split(full, delim);
}

namespace strings {

std::vector<string> Split(const string& full, const string& delim) {
  std::vector<string> res;
  size_t index = 0;
  for (;;) {
    size_t found = full.find_first_of(delim, index);
    if (found == string::npos) {
      break;
    }
    res.push_back(full.substr(index, found - index));
    index = found + 1;

    // Skip consecutive delimiters.
    while (index < full.size() &&
           delim.find_first_of(full[index]) != string::npos) {
      index++;
    }
  }

  res.push_back(full.substr(index));
  return res;
}

}  // namespace strings
