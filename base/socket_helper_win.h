// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_BASE_SOCKET_HELPER_WIN_H_
#define DEVTOOLS_GOMA_BASE_SOCKET_HELPER_WIN_H_

#ifdef _WIN32

// Note: In this port, we mix the use of SOCKET and int, which is okay for
//       32-bits but will trigger bunch of warnings for 64-bits
//       (SOCKET is UINT_PTR, and UINT_PTR is __w64 unsigned int)
//       It should be safe to ignore those warnings.

#pragma once
#include "basictypes.h"
#include "compiler_specific.h"
#include "config_win.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment (lib, "ws2_32")

typedef unsigned short sa_family_t;

int inet_aton(const char* input, struct in_addr* output);
int socketpair(sa_family_t domain, int type, int protocol, int socks[2]);
int async_socketpair(int socks[2]);

// Helper class to init/destroy winsock correctly.
// Instantiate this class object in your main().
class WinsockHelper {
 public:
  WinsockHelper();
  ~WinsockHelper();
  bool initialized() const { return initialized_; }

 private:
  bool initialized_;

  DISALLOW_COPY_AND_ASSIGN(WinsockHelper);
};

#endif  // _WIN32
#endif  // DEVTOOLS_GOMA_BASE_SOCKET_HELPER_WIN_H_
