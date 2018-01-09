// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_FILEFLAG_H_
#define DEVTOOLS_GOMA_LIB_FILEFLAG_H_

#ifndef _WIN32

namespace devtools_goma {

int SetFileDescriptorFlag(int fd, int flag);
int SetFileStatusFlag(int fd, int flag);

}  // namespace devtools_goma

#endif  // !_WIN32
#endif  // DEVTOOLS_GOMA_LIB_FILEFLAG_H_
