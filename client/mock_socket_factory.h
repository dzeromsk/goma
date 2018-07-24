// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_MOCK_SOCKET_FACTORY_H_
#define DEVTOOLS_GOMA_CLIENT_MOCK_SOCKET_FACTORY_H_

#include <string>

#include "absl/time/time.h"
#include "basictypes.h"
#include "scoped_fd.h"
#include "socket_factory.h"

#ifdef _WIN32
# include "socket_helper_win.h"
#else
# include <unistd.h>
#endif

using std::string;

namespace devtools_goma {

class WorkerThreadManager;

int OpenSocketPairForTest(int socks[2]);

// SocketFactory for test.
class MockSocketFactory : public SocketFactory {
 public:

  class SocketStatus {
   public:
    SocketStatus()
        : is_owned_(true),
          is_closed_(false),
          is_released_(false),
          is_err_(false) {
    }

    bool is_closed() { return is_closed_; }
    bool is_owned() { return is_owned_; }
    bool is_released() { return is_released_; }
    bool is_err() { return is_err_; }

   private:
    friend class MockSocketFactory;

    bool is_owned_; // true if the socket is owned by MockSocketFactory.
    bool is_closed_; // true if the socket is closed.
    bool is_released_;// true if the socket has been obtained once and released.
    bool is_err_; // true if the socket is closed with error.
  };

  // Does not take ownership of |socket_status|
  explicit MockSocketFactory(int sock, SocketStatus* socket_status = nullptr)
      : sock_(sock),
        dest_("mock:80"),
        host_name_("mock"),
        port_(80),
        is_owned_(true),
        socket_status_(socket_status) {
  }
  ~MockSocketFactory() override;
  bool IsInitialized() const override { return true; }

  ScopedSocket NewSocket() override;

  void ReleaseSocket(ScopedSocket&& sock) override;
  void CloseSocket(ScopedSocket&& sock, bool err) override;

  string DestName() const override { return dest_; }
  string host_name() const override { return host_name_; }
  int port() const override { return port_; }
  string DebugString() const override { return "MockSocketFactory"; }

  void set_dest(const string& dest) { dest_ = dest; }
  void set_host_name(const string& host_name) { host_name_ = host_name; }
  void set_port(int port) { port_ = port; }

  void set_is_owned(bool b) {
    is_owned_ = b;
    if (socket_status_ != nullptr) {
      socket_status_->is_owned_ = b;
    }
  }

 private:
  int sock_;
  string dest_;
  string host_name_;
  int port_;

  // |is_owned_| is used to hold the state for ~MockSocketFactory()
  // this value should be same with log_->is_owned_
  bool is_owned_;
  SocketStatus* socket_status_;
  DISALLOW_COPY_AND_ASSIGN(MockSocketFactory);
};

class MockSocketServer {
 public:
  // MockSocketServer create a new pool in wm and runs the following action
  // on a thread in the pool.
  explicit MockSocketServer(WorkerThreadManager* wm);
  ~MockSocketServer();

  // Test server will read from sock and store received data in buf.
  // Caller should set expected size to buf by buf->resize(N).
  // Once N bytes are read in buf, this action will finish.
  void ServerRead(int sock, string* buf);

  // Test server will write buf to sock.
  void ServerWrite(int sock, string buf);

  // Test server will close the sock.
  void ServerClose(int sock);

  // Test server will wait wihtout doing read/write to cause client timeout.
  void ServerWait(absl::Duration wait_time);

 private:
  void DoServerRead(int sock, string* buf);
  void DoServerWrite(int sock, string buf);
  void DoServerClose(int sock);
  void DoServerWait(absl::Duration wait_time);

  WorkerThreadManager* wm_;
  int pool_;

  DISALLOW_COPY_AND_ASSIGN(MockSocketServer);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_MOCK_SOCKET_FACTORY_H_
