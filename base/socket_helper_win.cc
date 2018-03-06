// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


// Note for 64-bit SOCKET usage:
// It's okay to cast between int and SOCKET since Windows Handle value will not
// exceed 2^24 according to Windows Internals. See
// http://blogs.technet.com/b/markrussinovich/archive/2009/09/29/3283844.aspx.

#ifdef _WIN32

#include "socket_helper_win.h"
#include "glog/logging.h"
#include "lockhelper.h"
#include "platform_thread.h"

#define WSA_VERSION MAKEWORD(2, 2)  // using winsock 2.2

int inet_aton(const char* input, struct in_addr* output) {
  return inet_pton(AF_INET, input, &output->s_addr);
}

/* socketpair.c
 * Copyright 2007, 2010 by Nathan C. Myers <ncm@cantrip.org>
 * This code is Free Software.  It may be copied freely, in original or
 * modified form, subject only to the restrictions that (1) the author is
 * relieved from all responsibilities for any use for any purpose, and (2)
 * this copyright notice must be retained, unchanged, in its entirety.  If
 * for any reason the author might be held responsible for any consequences
 * of copying or use, license is withheld.
 */
// Original version:
// https://github.com/ncm/selectable-socketpair/blob/master/socketpair.c
// This implementation can only be blocking and is not select-able.
// TODO: Use type SOCKET for socks[2] instead of type int.
int socketpair(sa_family_t domain, int type, int protocol, int socks[2]) {
  union {
    struct sockaddr_in inaddr;
    struct sockaddr addr;
  } addr;

  SOCKET listener;
  socklen_t addr_len = sizeof(addr.inaddr);
  if (socks == nullptr) {
    WSASetLastError(WSA_INVALID_PARAMETER);
    return SOCKET_ERROR;
  }

  listener = socket(domain, type, protocol);
  if (listener == INVALID_SOCKET) {
    return SOCKET_ERROR;
  }
  memset(&addr, 0, sizeof(addr));
  addr.inaddr.sin_family = domain;
  addr.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.inaddr.sin_port = 0;

  socks[0] = static_cast<int>(INVALID_SOCKET);
  socks[1] = static_cast<int>(INVALID_SOCKET);

  int reuse = 1;
  for (;;) {
    if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse,
                   (socklen_t)sizeof(reuse)) == -1) {
      break;
    }
    if (bind(listener, &addr.addr, sizeof(addr.inaddr)) == SOCKET_ERROR) {
      break;
    }
    memset(&addr, 0, sizeof(addr));
    if (getsockname(listener, &addr.addr, &addr_len) == SOCKET_ERROR) {
      break;
    }
    addr.inaddr.sin_family = AF_INET;
    addr.inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (listen(listener, 1) == SOCKET_ERROR) {
      break;
    }

    socks[0] = static_cast<int>(WSASocket(
        domain, type, protocol, nullptr, 0, 0));
    if (socks[0] == static_cast<int>(INVALID_SOCKET)) {
      break;
    }
    if (connect(socks[0], &addr.addr, sizeof(addr.inaddr)) == SOCKET_ERROR) {
      break;
    }

    socks[1] = static_cast<int>(accept(listener, nullptr, nullptr));
    if (socks[1] == static_cast<int>(INVALID_SOCKET)) {
      break;
    }
    closesocket(listener);
    return 0;
  }

  int last_error = WSAGetLastError();
  closesocket(listener);
  closesocket(socks[1]);
  closesocket(socks[0]);
  WSASetLastError(last_error);
  return SOCKET_ERROR;
}

namespace {

class ServerThread : public devtools_goma::PlatformThread::Delegate {
 public:
  // Creates |*listener| socket and starts listen at |*listener| on |*port|.
  // Returns WSA error code.  If success, returns 0.
  // |*port| is allocated from available port by system.
  // It is stored in host byte order.
  static DWORD StartListen(SOCKET* listener, int* port) {
    DCHECK(listener != nullptr);
    DCHECK(port != nullptr);
    *listener = INVALID_SOCKET;
    *port = 0;
    sockaddr_in inaddr = {};

    while (*port == 0) {
      *listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if (*listener == INVALID_SOCKET) {
        DWORD err = WSAGetLastError();
        LOG(ERROR) << "listen failed:" << err;
        return err;
      }
      memset(&inaddr, 0, sizeof(inaddr));
      inaddr.sin_family = AF_INET;
      inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      inaddr.sin_port = htons(0);
      socklen_t addr_len = sizeof(inaddr);

      unsigned long non_blocking = 1;
      if (ioctlsocket(*listener, FIONBIO, &non_blocking) == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        LOG(ERROR) << "socket non blocking failed:" << err;
        closesocket(*listener);
        *listener = INVALID_SOCKET;
        return err;
      }
      if (bind(*listener, (sockaddr*)&inaddr, addr_len) == SOCKET_ERROR) {
        // bind may fail if other process/thread uses the port.
        LOG(WARNING) << "bind failed:" << WSAGetLastError();
        closesocket(*listener);
        continue;
      }
      memset(&inaddr, 0, sizeof(inaddr));
      addr_len = sizeof(inaddr);
      if (getsockname(*listener, (sockaddr*)&inaddr, &addr_len)
          == SOCKET_ERROR) {
        DWORD err = WSAGetLastError();
        LOG(ERROR) << "getsockname failed:" << err;
        closesocket(*listener);
        *listener = INVALID_SOCKET;
        return err;
      }
      *port = ntohs(inaddr.sin_port);
    }
    if (listen(*listener, 1) == SOCKET_ERROR) {
      DWORD err = WSAGetLastError();
      LOG(ERROR) << "listen failed:" << err;
      closesocket(*listener);
      *listener = INVALID_SOCKET;
      return err;
    }
    return 0;
  }

  // ServerThread listen at |listener| and set accepted socket in |*accept|.
  // ServerThread will close |listener|.
  ServerThread(SOCKET listener, SOCKET* accept)
      : listener_(listener), accept_(accept), result_(WSAETIMEDOUT) {
    DCHECK_NE(listener_, INVALID_SOCKET);
    DCHECK_EQ(*accept_, INVALID_SOCKET);
  }

  ~ServerThread() {
    closesocket(listener_);
  }

  void ThreadMain() override {
    VLOG(1) << "socketpair ServerThread: start";
    fd_set r_set;
    SOCKET s = INVALID_SOCKET;
    for (;;) {
      timeval tv;
      tv.tv_sec = 2;
      tv.tv_usec = 0;  // timeout is two seconds
      FD_ZERO(&r_set);
      MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
      FD_SET(listener_, &r_set);
      MSVC_POP_WARNING();
      int r = select(static_cast<int>(listener_ + 1),
                     &r_set, nullptr, nullptr, &tv);
      if (r < 0) {
        LOG(WARNING) << "select error:" << r
                     << " result=" << WSAGetLastError();
        continue;
      } else if (r == 0) {
        LOG(WARNING) << "select timed-out";
        continue;
      }
      if (FD_ISSET(listener_, &r_set)) {
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        s = accept(listener_, (sockaddr*)&client_addr, &addr_len);
        if (s == INVALID_SOCKET) {
          result_ = WSAGetLastError();
          if (result_ == WSAEWOULDBLOCK) {
            continue;
          }
          DCHECK_NE(result_, 0);
          LOG(ERROR) << "accpet failed:" << result_;
          closesocket(s);
          return;
        }
        // accepted.
        *accept_ = s;
        result_ = 0;
        VLOG(1) << "socketpair ServerThread: ready";
        return;
      }
    }
  }

  int result() { return result_; }

 private:
  SOCKET listener_;
  SOCKET* accept_;
  int result_;

  DISALLOW_COPY_AND_ASSIGN(ServerThread);
};

class ClientThread : public devtools_goma::PlatformThread::Delegate {
 public:
  explicit ClientThread(SOCKET* client, int port)
      : client_(client), port_(port), result_(WSAETIMEDOUT) {}

  void ThreadMain() override {
    *client_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*client_ == INVALID_SOCKET) {
      result_ = WSAGetLastError();
      return;
    }

    sockaddr_in inaddr;
    memset(&inaddr, 0, sizeof(inaddr));
    inaddr.sin_family = AF_INET;
    inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    inaddr.sin_port = htons((unsigned short)port_);
    socklen_t addr_len = sizeof(inaddr);

    timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;  // timeout is two seconds
    fd_set w_set, e_set;
    FD_ZERO(&w_set);
    FD_ZERO(&e_set);

    for (;;) {
      int r = connect(*client_, (sockaddr*)&inaddr, addr_len);
      if (r != SOCKET_ERROR) {  // Connected immediately
        result_ = 0;
        return;
      }

      result_ = WSAGetLastError();
      if (result_ != WSAEWOULDBLOCK) {
        break;
      } else {
        // need select
        MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
        FD_SET(*client_, &w_set);
        MSVC_POP_WARNING();
        e_set = w_set;
        r = select(static_cast<int>(*client_ + 1), nullptr,
                   &w_set, &e_set, &tv);
        if (r == 0) {  // Connection timeout
          result_ = WSAETIMEDOUT;
          break;
        }
        if (FD_ISSET(*client_, &w_set) || FD_ISSET(*client_, &e_set)) {
          int len = sizeof(result_);
          if (getsockopt(*client_, SOL_SOCKET, SO_ERROR, (char*)&result_,
                         &len) >= 0) {  // Connection established
            return;
          }
          result_ = WSAGetLastError();
        } else {  // Unknown error in connect
          result_ = WSAGetLastError();
        }
      }
      break;
    }
    closesocket(*client_);
  }

  int result() { return result_; }

 private:
  SOCKET* client_;
  int port_;
  int result_;

  DISALLOW_COPY_AND_ASSIGN(ClientThread);
};

}  // namespace

// TODO: Use type SOCKET for socks[2].
int async_socketpair(int socks[2]) {
  if (socks == nullptr) {
    WSASetLastError(WSA_INVALID_PARAMETER);
    return SOCKET_ERROR;
  }
  SOCKET listener = INVALID_SOCKET;
  int port = 0;
  DWORD err = ServerThread::StartListen(&listener, &port);
  if (err != 0) {
    LOG(ERROR) << "StartListen failed:" << err;
    WSASetLastError(err);
    return SOCKET_ERROR;
  }
  DCHECK_NE(listener, INVALID_SOCKET);
  DCHECK_NE(port, 0);

  SOCKET server_socket = INVALID_SOCKET;
  SOCKET client_socket = INVALID_SOCKET;

  ServerThread server(listener, &server_socket);
  devtools_goma::PlatformThreadHandle server_thread_handle =
      devtools_goma::kNullThreadHandle;
  devtools_goma::PlatformThread::Create(&server, &server_thread_handle);

  // This will be blocked until server started listening.
  ClientThread client(&client_socket, port);
  devtools_goma::PlatformThreadHandle client_thread_handle =
      devtools_goma::kNullThreadHandle;
  devtools_goma::PlatformThread::Create(&client, &client_thread_handle);

  DWORD result = WaitForSingleObject(client_thread_handle, INFINITE);
  if (result == WAIT_OBJECT_0) {
    socks[1] = static_cast<int>(client_socket);
    if (client_socket == INVALID_SOCKET) {
      LOG(ERROR) << "client thread result=" << client.result();
    }
  } else {
    socks[1] = static_cast<int>(INVALID_SOCKET);
    LOG(ERROR) << "client wait error: result=" << result;
  }
  result = WaitForSingleObject(server_thread_handle, INFINITE);
  if (result == WAIT_OBJECT_0) {
    socks[0] = static_cast<int>(server_socket);
    if (server_socket == INVALID_SOCKET) {
      LOG(ERROR) << "server thread result=" << server.result();
    }
  } else {
    socks[0] = static_cast<int>(INVALID_SOCKET);
    LOG(ERROR) << "server wait error: result=" << result;
  }
  if (socks[0] != static_cast<int>(INVALID_SOCKET) &&
      socks[1] != static_cast<int>(INVALID_SOCKET)) {
    return 0;
  }
  return SOCKET_ERROR;
}

WinsockHelper::WinsockHelper() : initialized_(false) {
  WSADATA WSAData = {};
  if (WSAStartup(WSA_VERSION, &WSAData) != 0) {
    // Tell the user that we could not find a usable WinSock DLL.
    if (LOBYTE(WSAData.wVersion) != LOBYTE(WSA_VERSION) ||
        HIBYTE(WSAData.wVersion) != HIBYTE(WSA_VERSION)) {
        PLOG(ERROR) << "GOMA: Incorrect winsock version, required 2.2 and up";
    }
    WSACleanup();
  } else {
    initialized_ = true;
  }
}

WinsockHelper::~WinsockHelper() {
  if (initialized_) {
    WSACleanup();
  }
}

#endif  // _WIN32
