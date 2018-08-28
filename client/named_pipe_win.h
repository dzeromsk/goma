// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_WIN_H_

#ifdef _WIN32

#include <ostream>
#include <string>

#include "absl/strings/string_view.h"
#include "config_win.h"
#include "scoped_fd.h"

namespace devtools_goma {

class ScopedNamedPipe : public IOChannel {
 public:
  ScopedNamedPipe() : handle_(INVALID_HANDLE_VALUE) {}
  ScopedNamedPipe(ScopedNamedPipe&& other) : handle_(other.release()) {}
  explicit ScopedNamedPipe(HANDLE handle) : handle_(handle) {}
  ~ScopedNamedPipe() override;

  ScopedNamedPipe& operator=(ScopedNamedPipe&& other) {
    if (this == &other) {
      return *this;
    }
    reset(other.release());
    return *this;
  }

  ScopedNamedPipe(const ScopedNamedPipe&) = delete;
  ScopedNamedPipe& operator=(const ScopedNamedPipe&) = delete;

  void StreamWrite(std::ostream& os) const override;

  ssize_t Read(void* ptr, size_t len) const override;
  ssize_t Write(const void* ptr, size_t len) const override;
  ssize_t ReadWithTimeout(char* buf,
                          size_t bufsize,
                          absl::Duration timeout) const override;
  ssize_t WriteWithTimeout(const char* buf,
                           size_t bufsize,
                           absl::Duration timeout) const override;
  int WriteString(absl::string_view message,
                  absl::Duration timeout) const override;

  bool is_secure() const override { return true; }

  std::string GetLastErrorMessage() const override;

  bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
  HANDLE get() { return handle_; }
  HANDLE release() {
    HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return handle;
  }
  void reset(HANDLE handle);
  bool Close();

 private:
  HANDLE handle_;
};

}  // namespace devtools_goma

#endif  // _WIN32

#endif  // DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_WIN_H_
