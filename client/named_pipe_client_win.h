// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_CLIENT_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_CLIENT_WIN_H_

#ifdef _WIN32

#include <string>

#include "absl/time/time.h"
#include "named_pipe_win.h"

namespace devtools_goma {

class NamedPipeFactory {
 public:
  NamedPipeFactory(const std::string& name, absl::Duration timeout);
  ~NamedPipeFactory();

  NamedPipeFactory(const NamedPipeFactory&) = delete;
  NamedPipeFactory& operator=(const NamedPipeFactory&) = delete;

  ScopedNamedPipe New();

  const std::string& DestName() const {
    return name_;
  }

 private:
  const std::string name_;
  const absl::Duration timeout_;
};

}  // namespace devtools_goma

#endif  // _WIN32

#endif  // DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_CLIENT_WIN_H_
