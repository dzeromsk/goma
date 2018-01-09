// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMA_IPC_PEER_H_
#define DEVTOOLS_GOMA_CLIENT_GOMA_IPC_PEER_H_

#ifndef _WIN32
#include <unistd.h>
#include <sys/types.h>
#else
#include "config_win.h"
#endif

#include "scoped_fd.h"

namespace devtools_goma {

// Checks chan's peer is the same effective user.
// Returns true if it is the same user as local side.
// If peer_pid is not NULL (and platform could know peer pid), peer's pid
// will be set in *peer_pid.
bool CheckGomaIPCPeer(const IOChannel* chan, pid_t* peer_pid);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMA_IPC_PEER_H_
