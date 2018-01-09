// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SOCKET_POOL_H_
#define DEVTOOLS_GOMA_CLIENT_SOCKET_POOL_H_

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/types.h>
#endif
#include <time.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "basictypes.h"
#include "lockhelper.h"
#include "simple_timer.h"
#include "socket_factory.h"
#include "scoped_fd.h"
#include "unordered.h"

using std::string;

struct addrinfo;

namespace devtools_goma {

class SimpleTimer;

// TODO: template for ScopedSocket and ScopedNamedPipe.
class SocketPool : public SocketFactory {
 public:
  SocketPool(const string& host_name, int port);
  ~SocketPool() override;
  bool IsInitialized() const override;
  ScopedSocket NewSocket() override;

  // Releases the socket. The socket will be reused if NewSocket is called
  // within some period of time.
  void ReleaseSocket(ScopedSocket&& sock) override;

  // Closes the socket.
  // Marks the current address had error if err is true, so
  // it won't use the current address for some period of time when
  // it needs to open new connection.
  void CloseSocket(ScopedSocket&& sock, bool err) override;

  // Clears errors associated with addresses.
  void ClearErrors() override;

  string DestName() const override;
  string host_name() const override { return host_name_; }
  int port() const override { return port_; }

  size_t NumAddresses() const;

  string DebugString() const override;

 private:
  struct AddrData {
    AddrData();
    struct sockaddr_storage storage;
    size_t len;
    int ai_socktype;
    int ai_protocol;
    string name;
    time_t error_timestamp;

    const struct sockaddr* addr_ptr() const;
    void Invalidate();
    bool IsValid() const;
    bool InitFromIPv4Addr(const string& ipv4, int port);
    void InitFromAddrInfo(const struct addrinfo* ai);
  };
  class ScopedSocketList;

  // Resolves hostname:port and stores in addrs.
  static void ResolveAddress(const string& hostname, int port,
                             std::vector<AddrData>* addrs);

  // Initializes socket_pool.
  // Returns OK if at least one address could be available, and put
  // connected socket into socket_pool_.
  // Returns FAIL if no address is available.
  // Returns ERR_TIMEOUT if timeout.
  Errno InitializeUnlocked();

  // Sets error_timetamp in AddrData for sock to t
  void SetErrorTimestampUnlocked(int sock, time_t t);

  // This host:port is for means the address we will connect directly.
  // So, this can be either a destination address or a proxy address.
  string host_name_;
  int port_;

  Lock mu_;
  std::vector<AddrData> addrs_;
  AddrData* current_addr_;  // point in addrs_, or NULL.
  unordered_map<int, string> fd_addrs_;
  // TODO: use ScopedSocket. std::pair doesn't support movable yet?
  std::deque<std::pair<int, SimpleTimer>> socket_pool_;

  DISALLOW_COPY_AND_ASSIGN(SocketPool);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SOCKET_POOL_H_
