// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "socket_descriptor.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/un.h>
#include <sys/socket.h>
#else
#include <Winsock2.h>
#include "socket_helper_win.h"
#endif

#include <memory>

#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "callback.h"
#include "compiler_specific.h"
#include "glog/logging.h"
#include "worker_thread.h"

namespace devtools_goma {

SocketDescriptor::SocketDescriptor(ScopedSocket&& fd,
                                   WorkerThread::Priority priority,
                                   WorkerThread* worker)
    : fd_(std::move(fd)),
      priority_(priority),
      worker_(worker),
      readable_closure_(nullptr),
      writable_closure_(nullptr),
      last_time_(worker_->NowCached()),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          timeout_run_closure_(NewPermanentCallback(
              this, &SocketDescriptor::TimeoutClosure))),
      timeout_closure_(nullptr),
      read_in_queue_(false),
      write_in_queue_(false),
      timeout_in_queue_(false),
      active_read_(false),
      active_write_(false),
      write_poll_registered_(false),
      is_closed_(false),
      need_retry_(false) {
  thread_ = GetCurrentThreadId();
  CHECK(fd_.valid());
}

SocketDescriptor::~SocketDescriptor() {
  CHECK(!read_in_queue_);
  CHECK(!write_in_queue_);
  CHECK(!timeout_in_queue_);
  // Note that WorkerThreadManager::DeleteSocketDescriptor will take care of
  // unregistering closures from polling loop.
  // We do not need either to call UnregisterPollEvent or
  // CHECK(!write_poll_registered_).
}

void SocketDescriptor::NotifyWhenReadable(
    std::unique_ptr<PermanentClosure> closure) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  readable_closure_ = std::move(closure);
  last_time_ = worker_->NowCached();
  active_read_ = true;
  worker_->RegisterPollEvent(this, DescriptorEventType::kReadEvent);
  VLOG(1) << "Notify when " << fd_.get()
          << " readable" << readable_closure_.get();
}

void SocketDescriptor::NotifyWhenWritable(
    std::unique_ptr<PermanentClosure> closure) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  writable_closure_ = std::move(closure);
  last_time_ = worker_->NowCached();
  active_write_ = true;
  worker_->RegisterPollEvent(this, DescriptorEventType::kWriteEvent);
  write_poll_registered_ = true;
  VLOG(1) << "Notify when " << fd_.get()
          << " writable" << writable_closure_.get();
}

void SocketDescriptor::ClearReadable() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  VLOG(1) << "Clear " << fd_.get() << " readable " << readable_closure_.get();
  readable_closure_.reset();
  active_read_ = false;
  worker_->UnregisterPollEvent(this, DescriptorEventType::kReadEvent);
}

void SocketDescriptor::ClearWritable() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  VLOG(1) << "Clear " << fd_.get() << " writable " << writable_closure_.get();
  writable_closure_.reset();
  active_write_ = false;
  if (write_poll_registered_) {
    worker_->UnregisterPollEvent(this, DescriptorEventType::kWriteEvent);
    write_poll_registered_ = false;
  }
}

void SocketDescriptor::NotifyWhenTimedout(absl::Duration timeout,
                                          OneshotClosure* closure) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  DCHECK(!timeout_closure_);
  timeout_ = timeout;
  timeout_closure_.reset(closure);
  last_time_ = worker_->NowCached();
  worker_->RegisterTimeoutEvent(this);
}

void SocketDescriptor::ChangeTimeout(absl::Duration timeout) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  DCHECK(timeout_closure_);
  timeout_ = timeout;
  last_time_ = worker_->NowCached();
}

void SocketDescriptor::ClearTimeout() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  timeout_.reset();
  if (timeout_closure_) {
    timeout_closure_.reset();
  }
  worker_->UnregisterTimeoutEvent(this);
}

ssize_t SocketDescriptor::Read(void* ptr, size_t len) {
  CHECK_GT(len, 0) << "fd=" << fd_.get();
  need_retry_ = false;
  last_time_ = worker_->NowCached();
  ssize_t r = fd_.Read(ptr, len);
  if (r < 0)
    UpdateLastErrorStatus();
  if (r == 0)
    is_closed_ = true;
  return r;
}

ssize_t SocketDescriptor::Write(const void* ptr, size_t len) {
  CHECK_GT(len, 0) << "fd=" << fd_.get();
  need_retry_ = false;
  last_time_ = worker_->NowCached();
  ssize_t r = fd_.Write(ptr, len);
  if (r < 0)
    UpdateLastErrorStatus();
  return r;
}

bool SocketDescriptor::NeedRetry() const {
  return need_retry_;
}

int SocketDescriptor::ShutdownForSend() {
  need_retry_ = false;
  last_time_ = worker_->NowCached();
  int r;
#ifndef _WIN32
  r = shutdown(fd_.get(), SHUT_WR);
#else
  r = shutdown(fd_.get(), SD_SEND);
#endif
  if (r < 0)
    UpdateLastErrorStatus();
  return r;
}

bool SocketDescriptor::IsReadable() const {
  int n;
#ifndef _WIN32
  bool ioctl_success = ioctl(fd_.get(), FIONREAD, &n) != -1;
  if (!ioctl_success) {
    PLOG(WARNING) << "Failed to call ioctl: fd=" << fd_.get();
    return false;
  }
#else
  DWORD byte_returned;
  bool ioctl_success = WSAIoctl(fd_.get(), FIONREAD, nullptr, 0, &n, sizeof(n),
                                &byte_returned, nullptr, nullptr) == 0;
  if (!ioctl_success) {
    LOG_SYSRESULT(WSAGetLastError());
    LOG(WARNING) << "Failed to call WSAIoctl: fd=" << fd_.get();
    return false;
  }
#endif

  return n > 0;
}

void SocketDescriptor::StopRead() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  active_read_ = false;
}

void SocketDescriptor::StopWrite() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  active_write_ = false;
}

void SocketDescriptor::RestartRead() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  active_read_ = true;
}

void SocketDescriptor::RestartWrite() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  active_write_ = true;
  if (!write_poll_registered_) {
    VLOG(2) << "Register write again: fd=" << fd();
    worker_->RegisterPollEvent(this, DescriptorEventType::kWriteEvent);
    write_poll_registered_ = true;
  }
}

bool SocketDescriptor::wait_readable() const {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  return active_read_ && readable_closure_ != nullptr && !read_in_queue_;
}

bool SocketDescriptor::wait_writable() const {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  return active_write_ && writable_closure_ != nullptr && !write_in_queue_;
}

OneshotClosure* SocketDescriptor::GetReadableClosure() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  OneshotClosure* c =
      GetClosure(&read_in_queue_, &active_read_, readable_closure_.get());
  if (c != nullptr) {
    last_time_ = worker_->NowCached();
  }
  return c;
}

OneshotClosure* SocketDescriptor::GetWritableClosure() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  OneshotClosure* c =
      GetClosure(&write_in_queue_, &active_write_, writable_closure_.get());
  if (c != nullptr) {
    last_time_ = worker_->NowCached();
  }
  return c;
}

OneshotClosure* SocketDescriptor::GetTimeoutClosure() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  if (timeout_.has_value() &&
      worker_->NowCached() - last_time_ > *timeout_ &&
      !read_in_queue_ && !write_in_queue_ && !timeout_in_queue_) {
    return GetClosure(&timeout_in_queue_, nullptr, timeout_run_closure_.get());
  }
  return nullptr;
}

OneshotClosure* SocketDescriptor::GetClosure(
    bool* in_queue, bool* active, PermanentClosure* closure) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  if ((active == nullptr && (!active_read_ && !active_write_)) ||
      ((active != nullptr) && !(*active)))
    return nullptr;
  if (closure == nullptr)
    return nullptr;
  DCHECK(in_queue != nullptr);
  if (*in_queue)
    return nullptr;
  *in_queue = true;

  return NewCallback(this, &SocketDescriptor::RunCallback,
                     closure, in_queue, active);
}

void SocketDescriptor::RunCallback(
    PermanentClosure* closure, bool* in_queue, bool* active) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  DCHECK(closure != nullptr);
  DCHECK(in_queue != nullptr);
  DCHECK(*in_queue);
  *in_queue = false;
  if ((active == nullptr && (!active_read_ && !active_write_)) ||
      ((active != nullptr) && !(*active))) {
    // no need to delete closure.  it must be permanent closure.
    return;
  }
  closure->Run();
}

void SocketDescriptor::TimeoutClosure() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  if (read_in_queue_ || write_in_queue_)
    return;
  if (!active_read_ && !active_write_)
    return;
  if (timeout_.has_value() &&
      worker_->NowCached() - last_time_ > *timeout_) {
    // no need to delete closure. it deletes itself.
    OneshotClosure* closure = timeout_closure_.release();
    if (closure) {
      LOG(INFO) << "socket timeout fd=" << fd_.get()
                << " timeout=" << *timeout_;
      closure->Run();
    }
  }
}

void SocketDescriptor::UpdateLastErrorStatus() {
#ifndef _WIN32
  if (errno == EINTR || errno == EAGAIN) {
    need_retry_ = true;
    return;
  }
#endif

  char error_message[1024] = {0};
#ifndef _WIN32
  // Meaning of returned value of strerror_r is different between
  // XSI and GNU. Need to ignore.
  (void)strerror_r(errno, error_message, sizeof error_message);
#else
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, 0, WSAGetLastError(), 0,
                 error_message, sizeof error_message, 0);
#endif
  last_error_message_ = error_message;
}

void SocketDescriptor::UnregisterWritable() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  if (!active_write_ && write_poll_registered_) {
    worker_->UnregisterPollEvent(this, DescriptorEventType::kWriteEvent);
    write_poll_registered_ = false;
  }
}

string SocketDescriptor::PeerName() const {
  struct sockaddr_storage storage;
  memset(&storage, 0, sizeof storage);
  socklen_t len = sizeof(storage);
  int r = getpeername(fd_.get(),
                      reinterpret_cast<struct sockaddr*>(&storage), &len);
  if (r < 0) {
    PLOG(ERROR) << "getpeername";
    return "<unknown>";
  }
  char buf[128];
  static_assert(sizeof buf >= INET_ADDRSTRLEN, "buf is too small for inet");
  static_assert(sizeof buf >= INET6_ADDRSTRLEN, "buf is too small for inet6");
  switch (storage.ss_family) {
    case AF_INET:
      {
        struct sockaddr_in* in =
            reinterpret_cast<struct sockaddr_in*>(&storage);
        string name = inet_ntop(AF_INET, &in->sin_addr, buf, sizeof buf);
        return name;
      }
      break;
    case AF_INET6:
      {
        struct sockaddr_in6* in6 =
            reinterpret_cast<struct sockaddr_in6*>(&storage);
        string name = inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof buf);
        return name;
      }
      break;
#ifndef _WIN32
    case AF_UNIX:
      {
        struct sockaddr_un* un =
            reinterpret_cast<struct sockaddr_un*>(&storage);
        if (un->sun_path[0] == '\0') {
          if (un->sun_path[1] == '\0') {
            return "unix:<unnamed>";
          }
          return absl::StrCat("unix:<abstract>", &un->sun_path[1]);
        }
        return absl::StrCat("unix:", un->sun_path);
      }
#endif
    default:
      LOG(ERROR) << "unknown address family:" << storage.ss_family;
      return "<uknown-addr>";
  }
}

}  // namespace devtools_goma
