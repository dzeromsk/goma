// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content_cursor.h"

#include <algorithm>

namespace devtools_goma {

int ContentCursor::GetChar() {
  if (cur_ == buf_end()) {
    return EOF;
  }
  return *cur_++;
}

bool ContentCursor::Advance(size_t n) {
  if (cur_ + n <= buf_end()) {
    cur_ = cur_ + n;
    return true;
  }
  cur_ = buf_end();
  return false;
}

bool ContentCursor::SkipUntil(char c) {
  cur_ = std::find(cur_, buf_end(), c);
  return cur_ != buf_end();
}

}  // namespace devtools_goma
