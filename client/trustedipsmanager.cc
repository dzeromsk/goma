// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "trustedipsmanager.h"

#include <stdlib.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <sstream>

#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "util.h"

namespace devtools_goma {

TrustedIpsManager::TrustedIpsManager() {
  // Always allow from localhost.
  AddAllow("127.0.0.1");
}

TrustedIpsManager::~TrustedIpsManager() {
}

void TrustedIpsManager::AddAllow(const string& netspec) {
  trusted_.push_back(NetSpec(netspec));
}

bool TrustedIpsManager::IsTrustedClient(const struct in_addr& addr) const {
  for (std::vector<NetSpec>::const_iterator iter = trusted_.begin();
       iter != trusted_.end();
       ++iter) {
    if (iter->Match(addr))
      return true;
  }
  return false;
}

string TrustedIpsManager::DebugString() const {
  std::ostringstream out;
  out << "TrustedClients[";
  std::vector<string> res;
  for (std::vector<NetSpec>::const_iterator iter = trusted_.begin();
       iter != trusted_.end();
       ++iter) {
    res.push_back(iter->DebugString());
  }
  string netspecs = absl::StrJoin(res, ",");
  out << netspecs;
  out << "]";
  return out.str();
}

TrustedIpsManager::NetSpec::NetSpec(const string& netspec)
    : netmask_(0xffffffff) {
  std::vector<string> res = ToVector(absl::StrSplit(netspec, '/'));
  CHECK(res.size() == 1 || res.size() == 2)
      << "Wrong format of netspec:" << netspec;
  inet_aton(res[0].c_str(), &in_addr_);
  if (res.size() == 2) {
    int masklen = atoi(res[1].c_str());
    CHECK_LE(masklen, 32);
    if (masklen == 0) {
      netmask_ = 0;
    } else {
      netmask_ = 0xffffffff << (32 - masklen);
    }
  }
  in_addr_.s_addr = htonl(ntohl(in_addr_.s_addr) & netmask_);
}

TrustedIpsManager::NetSpec::~NetSpec() {
}

bool TrustedIpsManager::NetSpec::Match(const struct in_addr& addr) const {
  return (ntohl(addr.s_addr) & netmask_) == ntohl(in_addr_.s_addr);
}

string TrustedIpsManager::NetSpec::DebugString() const {
  std::ostringstream out;
  char buf[128];
  out << inet_ntop(AF_INET, const_cast<in_addr*>(&in_addr_), buf, sizeof buf)
      << "/" << std::hex << netmask_;
  return out.str();
}

}  // namespace devtools_goma
