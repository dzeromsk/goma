// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "mock_socket_factory.h"

#ifndef _WIN32
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#else
#include "socket_helper_win.h"
#endif

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "callback.h"
#include "compiler_specific.h"
#include "platform_thread.h"
#include "worker_thread.h"
#include "worker_thread_manager.h"

#include "glog/logging.h"

namespace devtools_goma {

int OpenSocketPairForTest(int socks[2]) {
#ifdef _WIN32
  // On Win32, no AF_UNIX (nor AF_LOCAL).
  sa_family_t af = AF_INET;
#else
  // On linux or so, socketpair only accepts AF_UNIX (or AF_LOCAL).
  int af = AF_UNIX;
#endif
  int r = socketpair(af, SOCK_STREAM, 0, socks);
  PLOG_IF(ERROR, r != 0) << "socketpair";
  LOG(INFO) << "socketpair r=" << r << " 0=" << socks[0] << " 1=" << socks[1];
  return r;
}

MockSocketFactory::~MockSocketFactory() {
  if (observer_ && is_owned_ && sock_ > 0) {
    observer_->WillCloseSocket(sock_);
  }
#ifndef _WIN32
  close(sock_);
#else
  closesocket(sock_);
#endif
  if (socket_status_ != nullptr) {
    socket_status_->is_closed_ = true;
  }
  LOG(INFO) << "close sock=" << sock_;
}

ScopedSocket MockSocketFactory::NewSocket() {
  CHECK(is_owned_);
  if (sock_ > 0) {
    set_is_owned(false);
  }
  LOG(INFO) << "new sock=" << sock_;
  return ScopedSocket(sock_);
}

void MockSocketFactory::ReleaseSocket(ScopedSocket&& sock) {
  LOG(INFO) << "release sock=" << sock;
  if (socket_status_ != nullptr) {
    socket_status_->is_released_ = true;
  }
  sock.release();
  set_is_owned(true);
}

void MockSocketFactory::CloseSocket(ScopedSocket&& sock, bool err) {
  if (observer_ && sock.get() == sock_) {
    observer_->WillCloseSocket(sock_);
  }
#ifndef _WIN32
  close(sock_);
#else
  closesocket(sock_);
#endif
  LOG(INFO) << "close sock=" << sock_;
  if (socket_status_ != nullptr) {
    socket_status_->is_err_ = err;
    socket_status_->is_closed_ = true;
  }
  sock_ = -1;
  CHECK(!is_owned_);
}

MockSocketServer::MockSocketServer(WorkerThreadManager* wm)
    : wm_(wm),
      actions_(0) {
#ifndef _WIN32
  // Do not die with SIGPIPE (i.e. write after client finished).
  signal(SIGPIPE, SIG_IGN);
#endif
  int n = wm_->num_threads();
  pool_ = wm_->StartPool(1, "mock_socket_server");
  while (wm_->num_threads() < n + 1U)
    absl::SleepFor(absl::Seconds(1));
}

MockSocketServer::~MockSocketServer() {
  AutoLock lock(&mu_);
  LOG(INFO) << "actions=" << actions_;
  while (actions_ > 0) {
    cond_.Wait(&mu_);
  }
  LOG(INFO) << "all action done";
}

void MockSocketServer::ServerRead(int sock, string* buf) {
  {
    AutoLock lock(&mu_);
    ++actions_;
  }
  wm_->RunClosureInPool(
      FROM_HERE,
      pool_,
      NewCallback(
          this, &MockSocketServer::DoServerRead, sock, buf),
      WorkerThread::PRIORITY_LOW);
}

void MockSocketServer::DoServerRead(int sock, string* buf) {
  const size_t read_size = buf->size();
  size_t nread = 0;
  LOG(INFO) << "DoServerRead sock=" << sock << " size=" << read_size;
  while (nread < read_size) {
#ifndef _WIN32
    int n = read(sock, &(*buf)[nread], read_size - nread);
#else
    int n = recv(sock, &(*buf)[nread], read_size - nread, 0);
#endif
    LOG(INFO) << "DoServerRead sock=" << sock << " " << (read_size - nread)
              << " => " << n
              << " data=" << string(buf->data() + nread, n);
    if (n < 0) {
      PLOG(ERROR) << "read";
      break;
    }
    if (n == 0) {
      break;
    }
    nread += n;
  }
  {
    AutoLock lock(&mu_);
    --actions_;
    cond_.Signal();
  }
}

void MockSocketServer::ServerWrite(int sock, string buf) {
  {
    AutoLock lock(&mu_);
    ++actions_;
  }
  wm_->RunClosureInPool(
      FROM_HERE,
      pool_,
      NewCallback(
          this, &MockSocketServer::DoServerWrite, sock, buf),
      WorkerThread::PRIORITY_LOW);
}

void MockSocketServer::DoServerWrite(int sock, string buf) {
  size_t written = 0;
  LOG(INFO) << "DoServerWrite sock=" << sock << " size=" << buf.size();
  while (written < buf.size()) {
#ifndef _WIN32
    int n = write(sock, &buf[written], buf.size() - written);
#else
    int n = send(sock, &buf[written], buf.size() - written, 0);
#endif
    LOG(INFO) << "DoServerWrite sock=" << sock << " " << (buf.size() - written)
              << " => " << n;
    if (n <= 0) {
      PLOG(ERROR) << "write";
      break;
    }
    written += n;
  }
  {
    AutoLock lock(&mu_);
    --actions_;
    cond_.Signal();
  }
}

void MockSocketServer::ServerClose(int sock) {
  {
    AutoLock lock(&mu_);
    ++actions_;
  }
  wm_->RunClosureInPool(
      FROM_HERE,
      pool_,
      NewCallback(
          this, &MockSocketServer::DoServerClose, sock),
      WorkerThread::PRIORITY_LOW);
}

void MockSocketServer::DoServerClose(int sock) {
  LOG(INFO) << "DoServerClose sock=" << sock;
#ifndef _WIN32
  close(sock);
#else
  closesocket(sock);
#endif
  {
    AutoLock lock(&mu_);
    --actions_;
    cond_.Signal();
  }
}

void MockSocketServer::ServerWait(absl::Duration wait_time) {
  {
    AutoLock lock(&mu_);
    ++actions_;
  }
  wm_->RunClosureInPool(
      FROM_HERE,
      pool_,
      NewCallback(
          this, &MockSocketServer::DoServerWait, wait_time),
      WorkerThread::PRIORITY_LOW);
}

void MockSocketServer::DoServerWait(absl::Duration wait_time) {
  LOG(INFO) << "DoServerWait " << wait_time;
  absl::SleepFor(wait_time);
  LOG(INFO) << "DoServerWait " << wait_time << " done";
  {
    AutoLock lock(&mu_);
    --actions_;
    cond_.Signal();
  }
}

}  // namespace devtools_goma
