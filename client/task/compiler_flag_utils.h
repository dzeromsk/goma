// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Functions for initializing or otherwise manipulating CompilerFlags after
// they are parsed, as part of the CompileTask process.

#ifndef DEVTOOLS_GOMA_CLIENT_TASK_COMPILER_FLAG_UTILS_H_
#define DEVTOOLS_GOMA_CLIENT_TASK_COMPILER_FLAG_UTILS_H_

namespace devtools_goma {

class ClangTidyFlags;

void InitClangTidyFlags(ClangTidyFlags* flags);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TASK_COMPILER_FLAG_UTILS_H_
