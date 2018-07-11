// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_INPUT_STREAM_H_
#define DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_INPUT_STREAM_H_

#include <memory>
#include <string>
#include <utility>

#include "content.h"

namespace devtools_goma {

class CppInputStream {
 public:
  // |content| must alive while CppinputStream is alive.
  CppInputStream(const Content* content, string filename)
      : content_(content),
        cur_(content_->buf()),
        line_(1),
        filename_(std::move(filename)) {}

  CppInputStream(const CppInputStream&) = delete;
  void operator=(const CppInputStream&) = delete;

  int line() const { return line_; }
  const char* cur() const { return cur_; }
  const char* begin() const { return content_->buf(); }
  const char* end() const { return content_->buf_end(); }
  size_t pos() const { return cur_ - content_->buf(); }

  const string& filename() const { return filename_; }

  void ConsumeChar();
  size_t GetLengthToCurrentFrom(const char* from, int lastchar) const;
  void Advance(int pos, int line);
  int GetChar();
  int GetCharWithBackslashHandling();
  void UngetChar(int c);
  int PeekChar() const;
  int PeekChar(int offset) const;
  void SkipWhiteSpaces();

 private:
  const Content* content_;
  const char* cur_;
  int line_;
  const string filename_;
};

// Utility functions
template <typename Char>
inline bool IsCppBlank(Char c) {
  return c == ' ' || c == '\t' || c == '\f' || c == '\v';
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CXX_INCLUDE_PROCESSOR_CPP_INPUT_STREAM_H_
