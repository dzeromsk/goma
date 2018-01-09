// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "goma_ipc_addr.h"

#include <string.h>

#include "glog/logging.h"

#ifndef UNIX_PATH_MAX
#define UNIX_PATH_MAX 108
#endif

namespace devtools_goma {

socklen_t InitializeGomaIPCAddress(const string& path, GomaIPCAddr* addr) {
  memset(addr, 0, sizeof(GomaIPCAddr));
#ifndef _WIN32
  // unix domain.
  size_t name_len = path.size();
  // Don't make unix domain socket invisible (i.e. use abstract socket address)
  // from file system as we need to run different compiler proxies both inside
  // and ouside chroot.  gomacc and compiler_proxy must run on the same
  // file system.
  // See b/5673736 for detail.
  CHECK_EQ(path[0], '/') << "bad socket path: " << path;
  if (name_len >= UNIX_PATH_MAX) {
    name_len = UNIX_PATH_MAX - 1;
  }
  addr->sun_family = AF_UNIX;
  char* sun_path = addr->sun_path;
  memcpy(sun_path, path.data(), name_len);
  addr->sun_path[name_len] = '\0';
#if defined(__MACH__) || defined(__FreeBSD__)
  addr->sun_len = SUN_LEN(addr);
  return sizeof(struct sockaddr_un);
#else
  return sizeof(addr->sun_family) + name_len;
#endif
#else  // _WIN32
  // TODO: Should use named pipe for IPC on Windows or use Chromium
  //                  base IPC instead.  For security reason, requester shall
  //                  be the same user.  Either named pipe or Chromium base
  //                  solves this concern.
  u_short server_port = static_cast<u_short>(atoi(path.c_str()));
  addr->sin_family = AF_INET;
  CHECK_GT(inet_pton(AF_INET, "127.0.0.1", &addr->sin_addr.s_addr), 0);
  addr->sin_port = htons(server_port);
  return sizeof(sockaddr_in);
#endif
}

}  // namespace devtools_goma
