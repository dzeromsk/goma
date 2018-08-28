// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "descriptor_poller.h"

#include "autolock_timer.h"
#include "socket_descriptor.h"
#include "glog/logging.h"
#include "simple_timer.h"

namespace devtools_goma {

DescriptorPollerBase::DescriptorPollerBase(
    std::unique_ptr<SocketDescriptor> poll_breaker,
    ScopedSocket&& poll_signaler)
    : poll_breaker_(std::move(poll_breaker)),
      poll_signaler_(std::move(poll_signaler)),
      poll_thread_(0) {
  CHECK(poll_breaker_);
  CHECK(poll_signaler_.valid());
}

bool DescriptorPollerBase::PollEvents(
    const DescriptorMap& descriptors,
    absl::Duration timeout,
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
    (*statp)->UpdateWaitTime(timer.GetDuration());
    timer.Start();
  }
  VLOG(3) << "poll on " << num_descriptors << " fds";
  int result = PollEventsInternal(timeout);
  VLOG(3) << "poll -> " << result;
  lock->Acquire();
  if (*statp != nullptr) {
    (*statp)->UpdateHoldTime(timer.GetDuration());
  }
  if (result == 0) {
    // timed-out
    VLOG(3) << "poll timed out";
    std::unique_ptr<EventEnumerator> enumerator(
        GetEventEnumerator(descriptors));
    SocketDescriptor* descriptor = nullptr;
    while ((descriptor = enumerator->Next()) != nullptr) {
      CHECK(descriptor);
      if (descriptor->fd() < 0) {
        VLOG(1) << "closed? " << descriptor;
        continue;
      }
      if (descriptor->fd() == poll_breaker_->fd()) {
        continue;
      }
      if (descriptor->priority() <= priority) {
        continue;
      }
      if (descriptor->wait_readable() || descriptor->wait_writable()) {
        OneshotClosure* closure = descriptor->GetTimeoutClosure();
        VLOG(2) << "fd " << descriptor->fd()
                << " poll timeout=" << timeout
                << " closure=" << closure;
        if (closure) {
          (*callbacks)[descriptor->priority()].push_back(closure);
        }
      }
    }
    return true;
  }
  if (result == -1) {
    if (errno != EINTR)
      PLOG(WARNING) << "poll failed with " << errno;
    return true;
  }

  bool poll_break = false;
  std::unique_ptr<EventEnumerator> enumerator(GetEventEnumerator(descriptors));
  SocketDescriptor* descriptor = nullptr;
  while ((descriptor = enumerator->Next()) != nullptr) {
    CHECK(descriptor);
    if (descriptor->fd() < 0) {
      VLOG(1) << "closed? " << descriptor;
      continue;
    }

    if (descriptor->fd() == poll_breaker_->fd()) {
      if (enumerator->IsReadable()) {
        // This is signalling from RunClosure() or sigchld.
        char buf[256];
        int n = poll_breaker_->Read(buf, sizeof(buf));
        PLOG_IF(WARNING, n < 0) << "poll breaker n=" << n;
        poll_break = true;
      }
      continue;
    }
    if (descriptor->priority() <= priority) {
      continue;
    }

    bool idle = true;
    if (enumerator->IsReadable()) {
      OneshotClosure* closure = descriptor->GetReadableClosure();
      VLOG(2) << "fd " << descriptor->fd() << " readable "
        << WorkerThread::Priority_Name(descriptor->priority())
        << " " << closure;
      if (closure) {
        (*callbacks)[descriptor->priority()].push_back(closure);
        idle = false;
      }
    }
    if (enumerator->IsWritable()) {
      OneshotClosure* closure = descriptor->GetWritableClosure();
      VLOG(2) << "fd " << descriptor->fd() << " writable "
        << WorkerThread::Priority_Name(descriptor->priority())
        << " " << closure;
      if (closure) {
        (*callbacks)[descriptor->priority()].push_back(closure);
        idle = false;
      }
    }
    if (idle) {
      OneshotClosure* closure = descriptor->GetTimeoutClosure();
      VLOG(2) << "fd " << descriptor->fd() << " idle "
        << WorkerThread::Priority_Name(descriptor->priority())
        << " " << closure;
      if (closure)
        (*callbacks)[descriptor->priority()].push_back(closure);
    }
  }
  return poll_break;
}

void DescriptorPollerBase::Signal() {
  ssize_t write_size = poll_signaler_.Write("", 1);
  LOG_IF(WARNING, write_size <= 0)
      << "poll signal write_size=" << write_size
      << " msg="<< poll_signaler_.GetLastErrorMessage();
}

}  // namespace devtools_goma
