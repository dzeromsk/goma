// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_POSIX_HELPER_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_POSIX_HELPER_WIN_H_

#ifndef _WIN32
#error Win32 only
#endif

// Windows POSIX emulation layer

#include "config_win.h"

#define R_OK    4               /* Test for read permission.  */
#define W_OK    2               /* Test for write permission.  */
#define X_OK    1               /* Test for execute permission.  */
#define F_OK    0               /* Test for existence.  */

namespace devtools_goma {

// access will return -1 if path is dir, different from posix.
// TODO: fix this?
int access(const char* path, int amode);

char *mkdtemp(char *tmpl);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_POSIX_HELPER_WIN_H_
