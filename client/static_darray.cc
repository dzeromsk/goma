// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "static_darray.h"

#include <glog/logging.h>

namespace devtools_goma {

int StaticDoubleArray::Lookup(const string& word) const {
  LookupHelper helper(this);
  for (size_t i = 0; i < word.length(); ++i) {
    if (!helper.Lookup(word[i]))
      return -1;
  }
  return helper.GetValue();
}

StaticDoubleArray::LookupHelper::LookupHelper(const StaticDoubleArray* array)
    : array_(array), index_(0) {
  DCHECK(array_);
}

bool StaticDoubleArray::LookupHelper::Lookup(char c) {
  DCHECK(0 <= index_ && index_ < array_->nodes_len);
  int next = array_->nodes[index_].base + array_->Encode(c);
  if (next < 0 || array_->nodes_len <= next)
    return false;
  if (index_ != array_->nodes[next].check)
    return false;
  index_ = next;
  return true;
}

int StaticDoubleArray::LookupHelper::GetValue() {
  DCHECK(0 <= index_ && index_ < array_->nodes_len);
  int next = array_->nodes[index_].base + array_->terminate_code;
  if (next < 0 || array_->nodes_len <= next)
    return -1;
  if (index_ != array_->nodes[next].check || array_->nodes[next].base > 0)
    return -1;
  return -array_->nodes[next].base;
}

}  // namespace devtools_goma
