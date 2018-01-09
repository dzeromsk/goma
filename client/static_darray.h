// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_STATIC_DARRAY_H_
#define DEVTOOLS_GOMA_CLIENT_STATIC_DARRAY_H_

#include <string>

#include "basictypes.h"

using std::string;

namespace devtools_goma {

struct StaticDoubleArray {
  struct Node { short base; short check; };
  StaticDoubleArray(const Node* n, int len, char base, int tcode)
      : nodes(n), nodes_len(len), encode_base(base), terminate_code(tcode) {}
  const Node* nodes;
  const int nodes_len;
  const char encode_base;
  const int terminate_code;

  // Returns the value for the given word.
  int Lookup(const string& word) const;

  // Incremental lookup helper.
  class LookupHelper {
   public:
    explicit LookupHelper(const StaticDoubleArray* array);
    bool Lookup(char c);

    // Finishes the lookup and returns the value.
    int GetValue();

   private:
    const StaticDoubleArray* array_;
    int index_;
  };

 private:
  int Encode(char c) const { return c - encode_base + 1; }

  DISALLOW_COPY_AND_ASSIGN(StaticDoubleArray);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_STATIC_DARRAY_H_
