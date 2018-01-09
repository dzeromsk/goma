// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "descriptor_poller.h"

#include "autolock_timer.h"
#include "socket_descriptor.h"
#include "glog/logging.h"
#include "simple_timer.h"

namespace devtools_goma {

DescriptorPollerBase::DescriptorPollerBase(SocketDescriptor* poll_breaker,
                                           ScopedSocket&& poll_signaler)
    : poll_thread_(0) {
  CHECK(poll_breaker);
  CHECK(poll_signaler.valid());
  poll_breaker_.reset(poll_breaker);
  poll_signaler_ = std::move(poll_signaler);
}

bool DescriptorPollerBase::PollEvents(
    const DescriptorMap& descriptors,
    int timeout_millisec,
    int priority,
    CallbackQueue* callbacks,
    Lock* lock, AutoLockStat** statp) EXCLUSIVE_LOCKS_REQUIRED(lock) {
  CHECK(lock);
  CHECK(statp);
  if (!poll_thread_) {
    poll_thread_ = GetCurrentThreadId();
  }
  CHECK(THREAD_ID_IS_SELF(poll_thread_));

  PreparePollEvents(descriptors);
  int num_descriptors = descriptors.size() + 1;

  SimpleTimer timer(SimpleTimer::NO_START);
  if (*statp != nullptr) {
    timer.Start();
  }
  lock->Release();
  if (*statp != nullptr) {
    (*statp)->UpdateWaitTime(timer.GetInNanoSeconds());
    timer.Start();
  }
  VLOG(3) << "poll on " << num_descriptors << " fds";
  int r = PollEventsInternal(timeout_millisec);
  VLOG(3) << "poll -> " << r;
  lock->Acquire();
  if (*statp != nullptr) {
    (*statp)->UpdateHoldTime(timer.GetInNanoSeconds());
  }
  if (r == 0) {
    // timed-out
    VLOG(3) << "poll timed out";
    std::unique_ptr<EventEnumerator> enumerator(
        GetEventEnumerator(descriptors));
    SocketDescriptor* d = nullptr;
    while ((d = enumerator->Next()) != nullptr) {
      CHECK(d);
      if (d->fd() < 0) {
        VLOG(1) << "closed? " << d;
        continue;
      }
      if (d->fd() == poll_breaker_->fd()) {
        continue;
      }
      if (d->priority() <= priority) {
        continue;
      }
      if (d->wait_readable() || d->wait_writable()) {
        OneshotClosure* closure = d->GetTimeoutClosure();
        VLOG(2) << "fd " << d->fd() << " poll timeout "
          << timeout_millisec << " msec"
          << " " << closure;
        if (closure) {
          (*callbacks)[d->priority()].push_back(closure);
        }
      }
    }
    return true;
  }
  if (r == -1) {
    if (errno != EINTR)
      PLOG(WARNING) << "poll failed with " << errno;
    return true;
  }

  bool poll_break = false;
  std::unique_ptr<EventEnumerator> enumerator(GetEventEnumerator(descriptors));
  SocketDescriptor* d = nullptr;
  while ((d = enumerator->Next()) != nullptr) {
    CHECK(d);
    if (d->fd() < 0) {
      VLOG(1) << "closed? " << d;
      continue;
    }

    if (d->fd() == poll_breaker_->fd()) {
      if (enumerator->IsReadable()) {
        // This is signalling from RunClosure() or sigchld.
        char buf[256];
        int n = poll_breaker_->Read(buf, sizeof(buf));
        PLOG_IF(WARNING, n < 0) << "poll breaker n=" << n;
        poll_break = true;
      }
      continue;
    }
    if (d->priority() <= priority) {
      continue;
    }

    bool idle = true;
    if (enumerator->IsReadable()) {
      OneshotClosure* closure = d->GetReadableClosure();
      VLOG(2) << "fd " << d->fd() << " readable "
        << WorkerThreadManager::Priority_Name(d->priority())
        << " " << closure;
      if (closure) {
        (*callbacks)[d->priority()].push_back(closure);
        idle = false;
      }
    }
    if (enumerator->IsWritable()) {
      OneshotClosure* closure = d->GetWritableClosure();
      VLOG(2) << "fd " << d->fd() << " writable "
        << WorkerThreadManager::Priority_Name(d->priority())
        << " " << closure;
      if (closure) {
        (*callbacks)[d->priority()].push_back(closure);
        idle = false;
      }
    }
    if (idle) {
      OneshotClosure* closure = d->GetTimeoutClosure();
      VLOG(2) << "fd " << d->fd() << " idle "
        << WorkerThreadManager::Priority_Name(d->priority())
        << " " << closure;
      if (closure)
        (*callbacks)[d->priority()].push_back(closure);
    }
  }
  return poll_break;
}

void DescriptorPollerBase::Signal() {
  int r = poll_signaler_.Write("", 1);
  LOG_IF(WARNING, r <= 0)
      << "poll signal r=" << r
      << " msg="<< poll_signaler_.GetLastErrorMessage();
}

}  // namespace devtools_goma
