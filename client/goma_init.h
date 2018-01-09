// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_GOMA_INIT_H_
#define DEVTOOLS_GOMA_CLIENT_GOMA_INIT_H_

#define GOMA_DECLARE_FLAGS_ONLY
#include "goma_flags.cc"

namespace devtools_goma {

void Init(int argc, char* argv[], const char* envp[]);
void InitLogging(const char* argv0);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMA_INIT_H_
