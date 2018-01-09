// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "socket_pool.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <map>
#include <sstream>

#include "autolock_timer.h"
#include "basictypes.h"
#include "fileflag.h"
#include "glog/logging.h"
#include "lockhelper.h"
#include "platform_thread.h"
#include "scoped_fd.h"
#include "simple_timer.h"

namespace devtools_goma {

// Do not use socket that is older than this, for HTTP Keep-Alive. It
// can be longer, but be on the safer side and do not bother with long
// timeouts.
const long long kIdleSocketTimeoutNanoseconds = 5LL * 1000 * 1000 * 1000;

// Do not use the address that we got error for this period.
// Note if we have keep-alive socket to that address, it will be used.
// if we got success after error from the addresss, we'll clear error status.
const int kErrorAddressTimeoutSecs = 60;

// Retry creation of socket pool for this period (milliseconds).
const int kSocketPoolSetupTimeoutInMs = 10 * 1000;
// Wait connection success for this period (milliseconds).
const int kConnTimeoutInMs = 3 * 1000;

SocketPool::SocketPool(const string& host_name, int port)
    : host_name_(host_name),
      port_(port),
      current_addr_(nullptr) {
  SimpleTimer timer;
  int32_t retry_backoff_ms = 50;
  while (timer.GetInMs() < kSocketPoolSetupTimeoutInMs) {
    Errno eno;
    {
      AUTOLOCK(lock, &mu_);
      eno = InitializeUnlocked();
      if (eno == OK)
        break;
    }
    if (eno == FAIL) {
      PlatformThread::Sleep(retry_backoff_ms);
      retry_backoff_ms *= 2;
      if (retry_backoff_ms > kConnTimeoutInMs)
        retry_backoff_ms = kConnTimeoutInMs;
    }
  }
  LOG_IF(WARNING, !IsInitialized()) << "failed to initialize socket pool in "
                                    << timer.GetInMs() << " msec."
                                    << " host_name=" << host_name
                                    << " port=" << port;
}

SocketPool::~SocketPool() {
  for (const auto& it : socket_pool_) {
    const int fd = it.first;
    if (observer_ != nullptr) {
      observer_->WillCloseSocket(fd);
    }
    ScopedSocket s(fd);
    s.Close();
  }
}

ScopedSocket SocketPool::NewSocket() {
  int new_fd = -1;
  std::vector<int> close_sockets;
  {
    // See if something from socket pool is re-usable.
    AUTOLOCK(lock, &mu_);
    while (!socket_pool_.empty()) {
      // If the socket has been idle for less than X seconds, use it.
      if (socket_pool_.front().second.GetInNanoSeconds() <
          kIdleSocketTimeoutNanoseconds) {
        new_fd = socket_pool_.front().first;
        VLOG(1) << "Reusing socket: " << new_fd
                << ", socket pool size: " << socket_pool_.size();
        socket_pool_.pop_front();
        break;
      } else {
        const int fd = socket_pool_.front().first;
        VLOG(1) << "Expiring too old socket: " << fd
                << ", socket pool size: " << socket_pool_.size();
        close_sockets.push_back(fd);
        fd_addrs_.erase(fd);
        socket_pool_.pop_front();
      }
    }
  }
  for (const auto& fd : close_sockets) {
    if (observer_ != nullptr) {
      observer_->WillCloseSocket(fd);
    }
    ScopedSocket s(fd);
    s.Close();
    // fd was removed fd_addrs_ above.
  }
  if (new_fd >= 0)
    return ScopedSocket(new_fd);

  int addrs_size;
  {
    AUTOLOCK(lock, &mu_);
    addrs_size = static_cast<int>(addrs_.size());
  }
  new_fd = -1;
  time_t error_time = 0;
  for (int retry = 0; retry < std::max(1, addrs_size); ++retry) {
    AddrData addr;
    {
      AUTOLOCK(lock, &mu_);
      if (new_fd >= 0) {
        SetErrorTimestampUnlocked(new_fd, error_time);
      }
      if (current_addr_ == nullptr || current_addr_->error_timestamp > 0) {
        LOG(INFO) << "need to retry with other address for " << host_name_;
        if (InitializeUnlocked() != OK) {
          DCHECK(current_addr_ == nullptr);
          LOG(ERROR) << "no other address available";
          return ScopedSocket();
        }
        DCHECK(!socket_pool_.empty());
        DCHECK_LT(socket_pool_.front().second.GetInNanoSeconds(),
                  kIdleSocketTimeoutNanoseconds);
        new_fd = socket_pool_.front().first;
        socket_pool_.pop_front();
        DCHECK_GE(new_fd, 0);
        return ScopedSocket(new_fd);
      }
      DCHECK(current_addr_ != nullptr);
      addr = *current_addr_;
    }

    ScopedSocket socket_fd(socket(addr.storage.ss_family, SOCK_STREAM, 0));
    if (!socket_fd.valid()) {
#ifndef _WIN32
      PLOG(WARNING) << "socket";
#else
      LOG(WARNING) << "socket error=" << WSAGetLastError();
#endif
      return socket_fd;
    }

    int r;
    // TODO: use nonblocking connect with timeout.
    while ((r = connect(socket_fd.get(), addr.addr_ptr(), addr.len)) < 0) {
      if (errno == EINTR) {
        continue;
      }
#ifndef _WIN32
      PLOG(WARNING) << "connect " << addr.name;
#else
      LOG(WARNING) << "connect " << addr.name
                   << " error=" << WSAGetLastError();
#endif
      break;
    }
    {
      AUTOLOCK(lock, &mu_);
      fd_addrs_.insert(std::make_pair(socket_fd.get(), addr.name));
    }
    if (r < 0) {
      new_fd = socket_fd.get();
      error_time = time(nullptr);
      continue;  // try other address.
    }
    if (!socket_fd.SetCloseOnExec()) {
      LOG(ERROR) << "failed to set FD_CLOEXEC";
      AUTOLOCK(lock, &mu_);
      fd_addrs_.erase(socket_fd.get());
      return ScopedSocket();
    }
    if (!socket_fd.SetNonBlocking()) {
      LOG(ERROR) << "failed to set O_NONBLOCK";
      AUTOLOCK(lock, &mu_);
      fd_addrs_.erase(socket_fd.get());
      return ScopedSocket();
    }
    return socket_fd;
  }
  LOG(ERROR) << "Too many retries in NewSocket";
  return ScopedSocket();
}

void SocketPool::ReleaseSocket(ScopedSocket&& sock) {
  AUTOLOCK(lock, &mu_);
  VLOG(1) << "pushing socket for recycling " << sock.get();
  int sock_fd = sock.get();
  socket_pool_.emplace_back(sock.release(), SimpleTimer());
  SetErrorTimestampUnlocked(sock_fd, 0);
}

void SocketPool::CloseSocket(ScopedSocket&& sock, bool err) {
  VLOG(1) << "close socket " << sock.get();
  if (observer_ != nullptr) {
    observer_->WillCloseSocket(sock.get());
  }
  AUTOLOCK(lock, &mu_);
  int sock_fd = sock.get();
  sock.Close();
  SetErrorTimestampUnlocked(sock_fd, (err ? time(nullptr) : 0));
  fd_addrs_.erase(sock_fd);
}

void SocketPool::ClearErrors() {
  LOG(INFO) << "Clear all errors associated to addresses.";
  AUTOLOCK(lock, &mu_);
  for (auto& addr : addrs_) {
    addr.error_timestamp = 0;
  }
}

void SocketPool::SetErrorTimestampUnlocked(int sock, time_t t) {
  const unordered_map<int, string>::const_iterator p = fd_addrs_.find(sock);
  if (p == fd_addrs_.end()) {
    LOG(ERROR) << "sock " << sock << " not found in fd_addrs";
    return;
  }
  const string& addr_name = p->second;
  // fast path. most case, current_addr_ is the addr for the sock.
  if (current_addr_ != nullptr && current_addr_->name == addr_name) {
    current_addr_->error_timestamp = t;
    return;
  }
  // slow path.
  for (auto& addr : addrs_) {
    if (addr.name == addr_name) {
      addr.error_timestamp = t;
      return;
    }
  }
  LOG(WARNING) << "sock " << sock << " addr:" << addr_name << " not found";
}

SocketPool::AddrData::AddrData()
    : len(0),
      ai_socktype(0),
      ai_protocol(0),
      error_timestamp(0) {
  memset(&storage, 0, sizeof storage);
}

const struct sockaddr* SocketPool::AddrData::addr_ptr() const {
  return reinterpret_cast<const struct sockaddr*>(&storage);
}

void SocketPool::AddrData::Invalidate() {
  len = 0;
}

bool SocketPool::AddrData::IsValid() const {
  return len > 0;
}

bool SocketPool::AddrData::InitFromIPv4Addr(const string& ipv4, int port) {
  struct sockaddr_in* addr_in =
      reinterpret_cast<struct sockaddr_in*>(&this->storage);
  this->len = sizeof(struct sockaddr_in);
  this->ai_socktype = SOCK_STREAM;
  this->ai_protocol = 0;
  this->name = ipv4;
  addr_in->sin_family = AF_INET;
  addr_in->sin_port = htons(static_cast<u_short>(port));
  if (inet_pton(AF_INET, ipv4.c_str(), &addr_in->sin_addr.s_addr) <= 0) {
    Invalidate();
    return false;
  }
  return true;
}

void SocketPool::AddrData::InitFromAddrInfo(const struct addrinfo* ai) {
  char buf[128];
  COMPILE_ASSERT(sizeof buf >= INET_ADDRSTRLEN, buf_too_small_inet);
  COMPILE_ASSERT(sizeof buf >= INET6_ADDRSTRLEN, buf_too_small_inet6);

  this->len = ai->ai_addrlen;
  memcpy(&this->storage, ai->ai_addr, this->len);
  this->ai_socktype = ai->ai_socktype;
  this->ai_protocol = ai->ai_protocol;
  switch (ai->ai_family) {
    case AF_INET:
      {
        struct sockaddr_in* in =
            reinterpret_cast<struct sockaddr_in*>(&this->storage);
        this->name = inet_ntop(AF_INET, &in->sin_addr, buf, sizeof buf);
      }
      break;
    case AF_INET6:
      {
        struct sockaddr_in6* in6 =
            reinterpret_cast<struct sockaddr_in6*>(&this->storage);
        this->name = inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof buf);
      }
      break;
    default:
      LOG(ERROR) << "Unknown address family:" << ai->ai_family;
  }
}

/* static */
void SocketPool::ResolveAddress(
    const string& hostname, int port,
    std::vector<SocketPool::AddrData>* addrs) {
  if (hostname.empty()) {
    LOG(ERROR) << "hostname is empty";
    return;
  }
  if (isdigit(hostname[0])) {
    // Try using it as IP address
    AddrData addr;
    if (addr.InitFromIPv4Addr(hostname, port)) {
      addrs->push_back(addr);
      return;
    }
  }
  sa_family_t afs[2] = { AF_INET, AF_INET6 };
  std::ostringstream port_oss;
  port_oss << port;
  const string port_string = port_oss.str();
  for (const auto& af : afs) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = af;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;
    int gai_error_code = getaddrinfo(
        hostname.c_str(), port_string.c_str(), &hints, &result);
    if (gai_error_code != 0) {
      if (af == AF_INET) {
        LOG(ERROR) << "getaddrinfo failed: " << gai_strerror(gai_error_code)
                   << " host:" << hostname
                   << " port:" << port_string
                   << " af:" << hints.ai_family;
      } else {
        // ok with no IPv6 addr.
        LOG(INFO) << "getaddrinfo failed: " << gai_strerror(gai_error_code)
                  << " host:" << hostname
                  << " port:" << port_string
                  << " af:" << hints.ai_family;
      }
      continue;
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
      AddrData addr;
      addr.InitFromAddrInfo(rp);
      addrs->push_back(addr);
    }
    freeaddrinfo(result);
  }
  LOG_IF(ERROR, addrs->empty()) << "Failed to resolve " << hostname;
}

class SocketPool::ScopedSocketList {
 public:
  // Doesn't take ownership of addrs.
  explicit ScopedSocketList(std::vector<AddrData>* addrs)
      : addrs_(addrs) {
    socks_.resize(addrs->size());
  }

  // Connect to initiate connection to all addrs with nonblocking socket.
  // Returns socket if connection is established.
  // Returns -1 otherwise.
  // *nfds will be the number of connection initiated.
  ScopedSocket Connect(int* nfds, AddrData** addr) {
    *nfds = 0;
    *addr = nullptr;
    time_t now = time(nullptr);
    time_t min_error_timestamp = now;
    for (const auto& address : *addrs_) {
      if (address.error_timestamp < min_error_timestamp) {
        min_error_timestamp = address.error_timestamp;
      }
    }

    for (size_t i = 0; i < addrs_->size(); ++i) {
      if ((*addrs_)[i].error_timestamp == min_error_timestamp) {
        // Use this addr even if it marked as error recently.
        // Most case, min_error_timestamp is 0 (some ip wasn't marked as error).
        // or this addr had error most long time ago in addrs.
        // Note that if len(addrs_)==1, the addr is used regardless of
        // error_timestamp to avoid "no other address available" by just
        // one error on the addr.
        // The addr, however, mignt not be used if connect fails.
        LOG_IF(WARNING, min_error_timestamp > 0)
            << "addrs[" << i << "] " << (*addrs_)[i].name
            << " min_error_timestamp=" << min_error_timestamp;
      } else {
        CHECK_GT((*addrs_)[i].error_timestamp, min_error_timestamp);
        if (now < (*addrs_)[i].error_timestamp + kErrorAddressTimeoutSecs) {
          LOG(WARNING) << "addrs[" << i << "] " << (*addrs_)[i].name
                       << " don't use until "
                       << ((*addrs_)[i].error_timestamp
                           + kErrorAddressTimeoutSecs)
                       << " error_timestamp=" << (*addrs_)[i].error_timestamp
                       << " now=" << now;
          continue;
        }
        // else error happened long time ago, so try again.
      }

      socks_[i] = ScopedSocket(
          socket((*addrs_)[i].storage.ss_family, SOCK_STREAM, 0));
      if (!socks_[i].valid()) {
#ifndef _WIN32
        PLOG(WARNING) << "socket:" << (*addrs_)[i].name;
#else
        LOG(WARNING) << "socket:" << (*addrs_)[i].name
                     << " error=" << WSAGetLastError();
#endif
        continue;
      }
      if (!socks_[i].SetCloseOnExec()) {
        LOG(WARNING) << "failed to set FD_CLOEXEC";
        socks_[i].Close();
        continue;
      }
      if (!socks_[i].SetNonBlocking()) {
        LOG(WARNING) << "failed to set O_NONBLOCK";
        socks_[i].Close();
        continue;
      }

      ++*nfds;
      // connect with nonblocking socket.
      if (connect(socks_[i].get(),
                  (*addrs_)[i].addr_ptr(),
                  (*addrs_)[i].len) == 0) {
        // If connect returns immediately on nonblocking socket,
        // it's fast enough so use it.
        *addr = &(*addrs_)[i];
        return std::move(socks_[i]);
      }
#ifdef WIN32
      if (WSAGetLastError() != WSAEWOULDBLOCK) {
        LOG(WARNING) << "connect to " << (*addrs_)[i].name
                     << " WSA:" << WSAGetLastError();
        socks_[i].Close();
        continue;
      }
#else
      if (errno != EINPROGRESS) {
        PLOG(WARNING) << "connect to " << (*addrs_)[i].name;
        socks_[i].Close();
        continue;
      }
#endif
    }
    return ScopedSocket();
  }

  // Poll nonblocking connect at most timeout_ms milliseconds.
  // Returns a connected socket, if connection has been established,
  // Returns -1 if poll has not yet finished.
  // nfds will be number of socket that is connecting.
  // if *nfds <= 0, no need to call Poll again.
  // TODO: reuse DescriptorPoller?
  ScopedSocket Poll(int timeout_ms, int* nfds, AddrData** addr);

 private:
  std::vector<AddrData>* addrs_;
  std::vector<ScopedSocket> socks_;

#ifdef WIN32
  fd_set fdset_;
#else
  std::vector<struct pollfd> pfds_;
#endif
  DISALLOW_COPY_AND_ASSIGN(ScopedSocketList);
};

#ifdef WIN32
ScopedSocket SocketPool::ScopedSocketList::Poll(
    int timeout_ms, int* nfds, AddrData** addr) {
  *nfds = 0;
  *addr = nullptr;
  fd_set exceptfds;
  FD_ZERO(&fdset_);
  FD_ZERO(&exceptfds);
  for (const auto& sock : socks_) {
    if (!sock.valid())
      continue;
    MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
    FD_SET(sock.get(), &fdset_);
    FD_SET(sock.get(), &exceptfds);
    MSVC_POP_WARNING();
    ++*nfds;
  }
  if (*nfds == 0) {
    return ScopedSocket();
  }
  TIMEVAL timeout;
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  int r = select(*nfds, nullptr, &fdset_, &exceptfds, &timeout);
  if (r == SOCKET_ERROR) {
    LOG(ERROR) << "connect select error="
               << WSAGetLastError();
    return ScopedSocket();
  }
  if (r == 0) {
    LOG(ERROR) << "connect timeout:" << timeout_ms << " msec";
    return ScopedSocket();
  }
  for (size_t i = 0; i < socks_.size(); ++i) {
    if (!socks_[i].valid())
      continue;
    if (FD_ISSET(socks_[i].get(), &fdset_)) {
      *addr = &(*addrs_)[i];
      return std::move(socks_[i]);
    }
    if (FD_ISSET(socks_[i].get(), &exceptfds)) {
      int val = 0;
      int val_size = sizeof(val);
      if (getsockopt(socks_[i].get(), SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&val), &val_size) != 0) {
        LOG(ERROR) << "getsockopt failed."
                   << " name=" << (*addrs_)[i].name
                   << " sock=" << socks_[i].get()
                   << " WSA:" << WSAGetLastError();
        continue;
      }
      if (val_size != sizeof(val)) {
        LOG(ERROR) << "getsockopt failed."
                   << " name=" << (*addrs_)[i].name
                   << " sock=" << socks_[i].get()
                   << " val_size=" << val_size;
        continue;
      }
      LOG(ERROR) << "getsockopt(SO_ERROR)."
                 << " name=" << (*addrs_)[i].name
                 << " sock=" << socks_[i].get()
                 << " val=" << val;
    }
  }
  return ScopedSocket();
}
#else
ScopedSocket SocketPool::ScopedSocketList::Poll(
    int timeout_ms, int* nfds, AddrData** addr) {
  *nfds = 0;
  *addr = nullptr;
  pfds_.resize(socks_.size());
  for (const auto& sock : socks_) {
    if (!sock.valid())
      continue;
    pfds_[*nfds].fd = sock.get();
    pfds_[*nfds].events = POLLOUT;
    ++*nfds;
  }
  if (*nfds == 0) {
    return ScopedSocket();
  }
  int r = poll(&pfds_[0], *nfds, timeout_ms);
  if (r == -1) {
    PLOG_IF(ERROR, errno != EINTR) << "connect poll error";
    return ScopedSocket();
  }
  if (r == 0) {
    PLOG(ERROR) << "connect timeout:" << timeout_ms << " msec";
    return ScopedSocket();
  }
  for (int i = 0; i < *nfds; ++i) {
    if (pfds_[i].revents & POLLOUT) {
      int fd = pfds_[i].fd;
      for (size_t j = 0; j < socks_.size(); ++j) {
        if (!socks_[j].valid())
          continue;
        if (socks_[j].get() == fd) {
          *addr = &(*addrs_)[j];
          return std::move(socks_[j]);
        }
      }
    }
  }
  return ScopedSocket();
}
#endif

Errno SocketPool::InitializeUnlocked() {
  // lock held.
  current_addr_ = nullptr;
  std::map<string, time_t> last_errors;
  for (const auto& addr : addrs_) {
    if (addr.error_timestamp > 0) {
      last_errors.insert(std::make_pair(addr.name, addr.error_timestamp));
    }
  }
  addrs_.clear();
  SimpleTimer timer;
  // TODO: avoid calling ResolveAddress if Initialize called immediately
  // again?
  ResolveAddress(host_name_, port_, &addrs_);
  for (auto& addr : addrs_) {
    const std::map<string, time_t>::const_iterator found =
        last_errors.find(addr.name);
    if (found != last_errors.end()) {
      addr.error_timestamp = found->second;
    }
    LOG(INFO) << host_name_ << " resolved as " << addr.name
              << " error_timestamp:" << addr.error_timestamp;
  }
  int resolve_ms = timer.GetInMs();
  if (resolve_ms > 1000) {
    LOG(ERROR) << "SLOW resolve " << host_name_ << " " << addrs_.size()
               << " in " << resolve_ms << " msec";
  } else {
    LOG(INFO) << "resolve " << host_name_ << " " << addrs_.size()
              << " in " << resolve_ms << " msec";
  }

  timer.Start();
  ScopedSocketList socks(&addrs_);

  int nfds;
  ScopedSocket s(socks.Connect(&nfds, &current_addr_));
  if (s.valid()) {
    DCHECK(current_addr_ != nullptr);
    DCHECK(current_addr_->IsValid());
    int connect_ms = timer.GetInMs();
    if (connect_ms > 1000) {
      LOG(ERROR) << "SLOW connected"
                 << ": use addr:" << current_addr_->name
                 << " for " << host_name_
                 << " in " << connect_ms << " msec";
    } else {
      LOG(INFO) << "connected"
                << ": use addr:" << current_addr_->name
                << " for " << host_name_
                << " in " << connect_ms << " msec";
    }
    fd_addrs_.insert(std::make_pair(s.get(), current_addr_->name));
    socket_pool_.emplace_back(s.release(), SimpleTimer());
    return OK;
  }
  if (nfds <= 0) {
    LOG(ERROR) << "Server at "
               << host_name_ << ":" << port_ << " not reachable.";
    DCHECK(current_addr_ == nullptr);
    return FAIL;
  }
  int remaining;
  while ((remaining = kConnTimeoutInMs - timer.GetInMs()) > 0) {
    s = socks.Poll(remaining, &nfds, &current_addr_);
    if (s.valid()) {
      break;
    }
    if (nfds <= 0) {
      break;
    }
  }
  LOG(INFO) << "connect done in " << timer.GetInMs() << " msec";
  if (!s.valid()) {
    DCHECK(current_addr_ == nullptr);
    LOG(ERROR) << "Server at "
               << host_name_ << ":" << port_ << " not reachable.";
    if (remaining <= 0)
      return ERR_TIMEOUT;
    return FAIL;
  }
  DCHECK(current_addr_ != nullptr);
  DCHECK(current_addr_->IsValid());
  LOG(INFO) << "use addr:" << current_addr_->name << " for " << host_name_;
  fd_addrs_.insert(std::make_pair(s.get(), current_addr_->name));
  socket_pool_.emplace_back(s.release(), SimpleTimer());
  return OK;
}

bool SocketPool::IsInitialized() const {
  AUTOLOCK(lock, &mu_);
  return current_addr_ != nullptr && current_addr_->IsValid();
}

string SocketPool::DestName() const {
  std::ostringstream ss;
  ss << host_name_ << ":" << port_;
  return ss.str();
}

size_t SocketPool::NumAddresses() const {
  AUTOLOCK(lock, &mu_);
  return addrs_.size();
}

string SocketPool::DebugString() const {
  std::ostringstream ss;
  ss << "dest:" << DestName();
  string name;
  size_t socket_pool_size = 0;
  size_t open_sockets = 0;
  {
    AUTOLOCK(lock, &mu_);
    if (current_addr_ != nullptr) {
      name = current_addr_->name;
    } else {
      name = "0.0.0.0";
    }
    socket_pool_size = socket_pool_.size();
    open_sockets = fd_addrs_.size();
  }
  ss << " addr:" << name;
  ss << " pool_size:" << socket_pool_size;
  ss << " open_sockets:" << open_sockets;
  return ss.str();
}

}  // namespace devtools_goma
