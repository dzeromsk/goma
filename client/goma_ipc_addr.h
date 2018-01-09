// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMA_IPC_ADDR_H_
#define DEVTOOLS_GOMA_CLIENT_GOMA_IPC_ADDR_H_

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#else
#include "socket_helper_win.h"
#endif

#include <string>

using std::string;

namespace devtools_goma {

#ifndef _WIN32
typedef struct sockaddr_un GomaIPCAddr;
static const int AF_GOMA_IPC = AF_UNIX;
#else
// Note on Windows design:
//
// Use Named pipe to restrict on the same computer.
//
// Initially, we used a separate port 18088 that is opened and listening to
// incoming request.  We believed this would be ok, since
// for Google Windows workstations, only one user at a time can log in.  When
// the user logged out, compiler_proxy will be forced to terminate since it is
// a user-launched process.  The listener is bound to localhost, therefore it
// accepts the traffic from within the machine only.  As a result,
// compiler_proxy will not be relaying requests from a different user.
//
// User fast switching can be a legitmate scenario and it will break goma one
// way or the other.  For a user to launch VC 2008, [s]he must be an admin.
//
// Possible attack factor is to web pages that issues request with XHR, since
// request will be sent regardless of cross origin.
// Another attack factor would be network API for chrome apps.
// Note: b/33103449
//
typedef struct sockaddr_in GomaIPCAddr;
static const int AF_GOMA_IPC = AF_INET;
#endif
socklen_t InitializeGomaIPCAddress(const string& path, GomaIPCAddr* addr);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMA_IPC_ADDR_H_
