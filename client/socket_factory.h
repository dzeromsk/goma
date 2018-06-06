// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SOCKET_FACTORY_H_
#define DEVTOOLS_GOMA_CLIENT_SOCKET_FACTORY_H_

#include <deque>
#include <string>

#include "basictypes.h"

#ifdef _WIN32
# include "socket_helper_win.h"
#endif

using std::string;

namespace devtools_goma {

class ScopedSocket;

class SocketFactoryObserver {
 public:
  virtual ~SocketFactoryObserver() {}
  virtual void WillCloseSocket(int sock) = 0;
};

// TODO: template for ScopedSocket and ScopedNamedPipe.
class SocketFactory {
 public:
  virtual ~SocketFactory() {}

  // Doesn't take ownership of observer.
  void SetObserver(SocketFactoryObserver* observer) {
    observer_ = observer;
  }

  // Returns true if socket factory is initialized.
  // Note:
  // Once the socket factory has a network issue, its IsInitialized become
  // false.  Unless NewSocket is called, it continues to return false.
  // TODO: remove this method if feasible.
  virtual bool IsInitialized() const = 0;

  // Returns new available socket.
  // Caller should return the socket to the socket factory by ReleaseSocket()
  // if socket could be reused or CloseSocket() if socket should be closed.
  // e.g.
  //   ScopedSocket s(socket_factory.NewSocket());
  //   // use s
  //   if (err) {
  //     socket_factory.CloseSocket(std::move(s), err);
  //   } else {
  //     socket_factory.ReleaseSocket(std::move(s));
  //     // or socket_factory.CloseSocket(std::move(s), false);
  //   }
  // Note: NewSocket() can be called even if IsInitialized() is false.
  virtual ScopedSocket NewSocket() = 0;

  // Releases used socket to socket factory.
  // The returned socket may be reused for NewSocket().
  // When it is about to close the socket, it will notify observer if the
  // observer is set.  Actual timing to close the socket is implementation
  // dependent.
  // Don't release a socket that an error happened, or that won't be reused.
  // Use CloseSocket() instead.
  virtual void ReleaseSocket(ScopedSocket&& sock) = 0;

  // Closes used socket. It will notify observer if the observer is set.
  virtual void CloseSocket(ScopedSocket&& sock, bool err) = 0;

  // Destination name in form of "host:port".
  virtual string DestName() const = 0;
  virtual string host_name() const { return ""; }
  virtual int port() const { return -1; }

  virtual string DebugString() const = 0;

 protected:
  SocketFactory() : observer_(NULL) {}

  SocketFactoryObserver* observer_;

 private:
  DISALLOW_COPY_AND_ASSIGN(SocketFactory);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SOCKET_FACTORY_H_
