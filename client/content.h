// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CONTENT_H_
#define DEVTOOLS_GOMA_CLIENT_CONTENT_H_

#include <memory>
#include <string>

#include "basictypes.h"
#include "string_piece.h"

using std::string;

namespace devtools_goma {

class ScopedFd;

class Content final {
 public:
  // Creates content from a file. nullptr will be returned if an error
  // occured e.g. a file does not exist.
  static std::unique_ptr<Content> CreateFromFile(const string& filepath);
  static std::unique_ptr<Content> CreateFromString(const string& str);
  static std::unique_ptr<Content> CreateFromContent(const Content& content);

  // The content of |buf| is copied, so Content won't own |buf|.
  // CreateFromBuffer allocates new memory in function.
  // It is prefer to use CreateFromBuffer when
  // |len| is smaller than size of |buf|.
  static std::unique_ptr<Content> CreateFromBuffer(const char* buf, size_t len);
  static std::unique_ptr<Content> CreateFromUnique(
      std::unique_ptr<const char[]> buf, size_t len);

  // Creates content from file descriptor. This method also takes |filesize|
  // so that we can skip calling stat. The argument |filepath| is used for
  // logging purpose.
  static std::unique_ptr<Content> CreateFromFileDescriptor(
      const string& filepath, const ScopedFd& fd, size_t filesize);

  StringPiece ToStringPiece() const { return StringPiece(buf_.get(), size()); }
  const char* buf() const { return buf_.get(); }
  const char* buf_end() const { return buf_end_; }
  size_t size() const { return buf_end() - buf(); }

 private:
  Content(std::unique_ptr<const char[]> buf, const char* buf_end)
      : buf_(std::move(buf)), buf_end_(buf_end) {}

  std::unique_ptr<const char[]> buf_;
  const char* buf_end_;

  DISALLOW_COPY_AND_ASSIGN(Content);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CONTENT_H_
