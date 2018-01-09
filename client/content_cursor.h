// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CONTENT_CURSOR_H_
#define DEVTOOLS_GOMA_CLIENT_CONTENT_CURSOR_H_

#include <memory>

#include "content.h"

namespace devtools_goma {

// ContentCursor is a cursor that runs on Content. It owns Content.
class ContentCursor final {
 public:
  explicit ContentCursor(std::unique_ptr<Content> content)
      : content_(std::move(content)),
        cur_(content_->buf()) {
  }

  ContentCursor(const ContentCursor&) = delete;
  void operator=(const ContentCursor&) = delete;

  const char* buf() const { return content_->buf(); }
  const char* buf_end() const { return content_->buf_end(); }
  const char* cur() const { return cur_; }

  int GetChar();

  // Advance cursor in |n| characters.
  // Returns true if it's possible.
  // If |n| is too large, cursur will point the end of the buffer.
  bool Advance(size_t n);

  // Skip until |c| is found. Returns true if |c| is found.
  // When true is returned, *cur() must be |c|.
  bool SkipUntil(char c);

 private:
  std::unique_ptr<Content> content_;
  const char* cur_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CONTENT_CURSOR_H_
