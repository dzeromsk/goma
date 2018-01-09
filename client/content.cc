// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "content.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
# include "config_win.h"
#endif
#include "scoped_fd.h"

#include <glog/logging.h>

namespace devtools_goma {

// static
std::unique_ptr<Content> Content::CreateFromFile(const string& filepath) {
  ScopedFd fd(ScopedFd::OpenForRead(filepath));
  if (!fd.valid())
    return nullptr;
  size_t len = 0;
  if (!fd.GetFileSize(&len)) {
    PLOG(ERROR) << "fd:" << fd << " filepath:" << filepath;
    return nullptr;
  }

  return CreateFromFileDescriptor(filepath, fd, len);
}

// static
std::unique_ptr<Content> Content::CreateFromFileDescriptor(
    const string& filepath, const ScopedFd& fd, size_t filesize) {
  DCHECK(fd.valid());

  std::unique_ptr<char[]> buf(new char[filesize + 1]);
  CHECK(buf.get() != nullptr) << "filepath:" << filepath
                              << "filesize:" << filesize;
  char* buf_end = buf.get() + filesize;
  *buf_end = '\0';
  size_t offset = 0;
  while (offset < filesize) {
    ssize_t actual_read = fd.Read(buf.get() + offset, filesize - offset);
    if (actual_read <= 0) {
      PLOG(ERROR) << "fd: " << fd << " filepath:" << filepath
                  << " offset: " << offset
                  << " actual_read: " << actual_read;
      return nullptr;
    }
    offset += actual_read;
  }

  if (offset != filesize) {
    LOG(ERROR) << "size mismatch filepath:" << filepath << " fd:" << fd
               << " offset:" << offset << " filesize:" << filesize;
    return CreateFromUnique(std::move(buf), offset);
  }

  return CreateFromUnique(std::move(buf), filesize);
}

// static
std::unique_ptr<Content> Content::CreateFromString(const string& str) {
  std::unique_ptr<char[]> buf(new char[str.length() + 1]);
  CHECK(buf != nullptr);
  memcpy(static_cast<void*>(buf.get()),
         static_cast<const void*>(str.data()),
         str.length());
  return CreateFromUnique(std::move(buf), str.length());
}

// static
std::unique_ptr<Content> Content::CreateFromContent(const Content& content) {
  const size_t content_length = content.size();
  return CreateFromBuffer(content.buf_.get(), content_length);
}

// static
std::unique_ptr<Content> Content::CreateFromBuffer(const char* buffer,
                                                   size_t len) {
  std::unique_ptr<char[]> new_buffer(new char[len + 1]);
  CHECK(new_buffer != nullptr);
  memcpy(new_buffer.get(), buffer, len);
  new_buffer[len] = '\0';
  return CreateFromUnique(std::move(new_buffer), len);
}

// static
std::unique_ptr<Content> Content::CreateFromUnique(
    std::unique_ptr<const char[]> buffer, size_t len) {
  const char* buffer_end = buffer.get() + len;
  return std::unique_ptr<Content>(new Content(std::move(buffer), buffer_end));
}

}  // namespace devtools_goma
