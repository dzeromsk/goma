// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_input_stream.h"

#include <glog/logging.h>

namespace devtools_goma {

void CppInputStream::ConsumeChar() {
  line_ += (*cur_ == '\n');
  ++cur_;
}

size_t CppInputStream::GetLengthToCurrentFrom(
    const char* from, int lastchar) const {
  return cur_ - from - (lastchar == EOF ? 0 : 1);
}

void CppInputStream::Advance(int pos, int line) {
  this->line_ += line;
  cur_ += pos;
}

int CppInputStream::GetChar() {
  DCHECK(cur_);
  if (cur_ >= content_->buf_end())
    return EOF;
  line_ += (*cur_ == '\n');
  return *cur_++;
}

int CppInputStream::GetCharWithBackslashHandling() {
  int c = GetChar();
  while (c == '\\') {
    const char* prev = cur();
    if (PeekChar() == '\r')
      Advance(1, 0);
    if (PeekChar() == '\n')
      Advance(1, 1);
    if (prev == cur())
      return c;
    c = GetChar();
  }
  return c;
}

void CppInputStream::UngetChar(int c) {
  if (c != EOF) {
    cur_--;
    if (c == '\n')
      line_--;
  }
}

int CppInputStream::PeekChar() const {
  DCHECK(cur_);
  if (cur_ >= content_->buf_end())
    return EOF;
  return *cur_;
}

int CppInputStream::PeekChar(int offset) const {
  DCHECK(cur_);
  if (cur_ + offset >= content_->buf_end())
    return EOF;
  return *(cur_ + offset);
}

void CppInputStream::SkipWhiteSpaces() {
  int c = GetChar();
  while (IsCppBlank(c)) {
    c = GetChar();
    if (c == '\\') {
      c = GetChar();
      if (c == '\r')
        c = GetChar();
      if (c == '\n')
        c = GetChar();
    }
  }
  UngetChar(c);
}

}  // namespace devtools_goma
