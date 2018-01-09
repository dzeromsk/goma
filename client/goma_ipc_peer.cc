// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "goma_ipc_peer.h"

#ifndef _WIN32
#include <sys/socket.h>
#if defined(__MACH__) || defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/ucred.h>
#endif
#include <sys/un.h>
#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#endif
#endif  // _WIN32

#include "glog/logging.h"

namespace devtools_goma {

#ifndef _WIN32
#ifdef __linux__
// hack for fakeroot
uid_t real_geteuid() {
  return syscall(SYS_geteuid);
}
#else
uid_t real_geteuid() {
  return geteuid();
}
#endif  // __linux__
#endif  // _WIN32

bool CheckGomaIPCPeer(const IOChannel* chan, pid_t* peer_pid) {
#ifdef _WIN32
  // We only trust named pipe, and don't trust socket.
  // see goma_ipc_addr.h.
  return chan->is_secure();
#elif defined(__MACH__) || defined(__FreeBSD__)
  // ScopedSocket's fd is valid socket descriptor.
  // TODO: better interface on IOChannel?
  int sock = static_cast<const ScopedSocket*>(chan)->get();
  struct xucred peer_cred;
  socklen_t peer_cred_len = sizeof(peer_cred);
  if (getsockopt(sock, 0, LOCAL_PEERCRED, &peer_cred, &peer_cred_len) < 0) {
    LOG(WARNING) << "cannot get peer credential. Not a unix socket?";
    return false;
  }
  if (peer_cred.cr_version != XUCRED_VERSION) {
    LOG(WARNING) << "credential version mismatch:"
                 << " cr_version=" << peer_cred.cr_version
                 << " XUCRED_VERSION=" << XUCRED_VERSION;
    return false;
  }
  // darwin doesn't have pid in cred structure.
  // TODO: find another way to get peer pid.
  if (peer_cred.cr_uid != geteuid()) {
    LOG(WARNING) << "uid mismatch peer=" << peer_cred.cr_uid
                 << " self=" << geteuid();
    return false;
  }
  return true;
#else
  // ScopedSocket's fd is valid socket descriptor.
  // TODO: better interface on IOChannel?
  int sock = static_cast<const ScopedSocket*>(chan)->get();
  struct ucred peer_cred;
  socklen_t peer_cred_len = sizeof(peer_cred);
  if (getsockopt(sock, SOL_SOCKET, SO_PEERCRED,
                 reinterpret_cast<void*>(&peer_cred),
                 &peer_cred_len) < 0) {
    LOG(WARNING) << "cannot get peer credential. Not a unix socket?";
    return false;
  }
  VLOG(3) << "peer_cred pid=" << peer_cred.pid << " uid=" << peer_cred.uid;
  if (peer_pid != nullptr)
    *peer_pid = peer_cred.pid;
  uid_t real_euid = real_geteuid();
  uid_t euid = geteuid();
  if (peer_cred.uid != real_euid && peer_cred.uid != euid) {
    LOG(WARNING) << "uid mismatch peer=" << peer_cred.uid
                 << " self=" << euid << "/real=" << real_euid;
    return false;
  }
  return true;
#endif
}

}  // namespace devtools_goma
