// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_TRUSTEDIPSMANAGER_H_
#define DEVTOOLS_GOMA_CLIENT_TRUSTEDIPSMANAGER_H_

#ifndef _WIN32
#include <netinet/in.h>
#else
#include "socket_helper_win.h"
#endif

#include <string>
#include <vector>

#include "basictypes.h"

using std::string;

namespace devtools_goma {

class TrustedIpsManager {
 public:
  TrustedIpsManager();
  ~TrustedIpsManager();

  // Adds "netspec" as trusted network.
  // "netspec" is dotted-decimal IPv4 address with or without netmask length.
  // e.g. "127.0.0.1", "192.168.1.1/24".
  void AddAllow(const string& netspec);

  bool IsTrustedClient(const struct in_addr& addr) const;

  string DebugString() const;

 private:
  class NetSpec {
   public:
    explicit NetSpec(const string& netspec);
    ~NetSpec();

    bool Match(const struct in_addr& addr) const;

    string DebugString() const;

   private:
    struct in_addr in_addr_;
    int netmask_;
  };
  std::vector<NetSpec> trusted_;

  DISALLOW_COPY_AND_ASSIGN(TrustedIpsManager);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TRUSTEDIPSMANAGER_H_
