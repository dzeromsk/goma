// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef _WIN32

#include "lib/fileflag.h"

#include <fcntl.h>
#include <unistd.h>

#include "glog/logging.h"

namespace devtools_goma {

int SetFileDescriptorFlag(int fd, int flag) {
  int old_flag = fcntl(fd, F_GETFD);
  if (old_flag == -1) {
    PLOG(ERROR) << "Cannot GETFD for fd:" << fd;
    return -1;
  }
  if (fcntl(fd, F_SETFD, old_flag | flag) == -1) {
    PLOG(ERROR) << "Cannot SETFD for fd:" << fd;
    return -1;
  }
  return 0;
}

int SetFileStatusFlag(int fd, int flag) {
  int old_flag = fcntl(fd, F_GETFL);
  if (old_flag == -1) {
    PLOG(ERROR) << "Cannot GETFL for fd:" << fd;
    return -1;
  }
  if (fcntl(fd, F_SETFL, old_flag | flag) == -1) {
    PLOG(ERROR) << "Cannot SETFL for fd:" << fd;
    return -1;
  }
  return 0;
}

}  // namespace devtools_goma
#endif  // !_WIN32
