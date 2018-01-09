// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_BASE_JOIN_H_
#define DEVTOOLS_GOMA_BASE_JOIN_H_

#include <string>
using std::string;

template <class Container>
void JoinStrings(const Container& components,
                 const string& delim,
                 string* result) {
  for (typename Container::const_iterator iter = components.begin();
       iter != components.end();
       ++iter) {
    if (iter != components.begin())
      *result += delim;
    *result += *iter;
  }
}

namespace strings {

template <class Container>
string Join(const Container& components, const string& delim) {
  string s;
  JoinStrings(components, delim, &s);
  return s;
}

}  // namespace strings

// absl compatibility layer
namespace absl {

template<class Container>
string StrJoin(const Container& components, const string& delim) {
  return strings::Join(components, delim);
}

}  // namespace absl

#endif  // DEVTOOLS_GOMA_BASE_JOIN_H_
