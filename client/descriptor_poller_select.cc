// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "descriptor_poller.h"

#include <algorithm>
#include <vector>

#ifndef _WIN32
# include <limits.h>
# include <signal.h>
# include <sys/ioctl.h>
# include <sys/socket.h>
# include <sys/wait.h>
#else
# include "socket_helper_win.h"
#endif

#include "absl/base/call_once.h"
#include "absl/memory/memory.h"
#include "absl/time/time.h"
#include "counterz.h"
#include "glog/logging.h"
#include "socket_descriptor.h"

namespace devtools_goma {

class SelectDescriptorPoller : public DescriptorPollerBase {
 public:
  SelectDescriptorPoller(std::unique_ptr<SocketDescriptor> breaker,
                         ScopedSocket&& poll_signaler)
      : DescriptorPollerBase(std::move(breaker), std::move(poll_signaler)),
        max_fd_(-1) {
    absl::call_once(s_init_once_, LogDescriptorPollerType);
    // Socket number ranges from 1 to 32767 on Windows, where the FD_SETSIZE is
    // 64. There's no guarantee on Windows that the value of socket fd is
    // smaller than FD_SETSIZE.
#ifndef _WIN32
    CHECK_LT(poll_breaker()->fd(), FD_SETSIZE);
#endif
  }

  static void LogDescriptorPollerType() {
    LOG(INFO) << "descriptor_poller will use \"select\"";
  }

  // No-op. We register polling descriptors in PreparePollEvents.
  void RegisterPollEvent(SocketDescriptor*, EventType) override {}
  void UnregisterPollEvent(SocketDescriptor*, EventType) override {}
  void RegisterTimeoutEvent(SocketDescriptor*) override {}
  void UnregisterTimeoutEvent(SocketDescriptor*) override {}
  void UnregisterDescriptor(SocketDescriptor*) override {}

 protected:
  void PreparePollEvents(const DescriptorMap& descriptors) override {
    FD_ZERO(&read_fd_);
    FD_ZERO(&write_fd_);

    max_fd_ = poll_breaker()->fd();
    int fd = poll_breaker()->fd();

    std::vector<SocketDescriptor*> waiting_descriptors;
    for (const auto& iter : descriptors) {
      SocketDescriptor* d = iter.second.get();
      fd = d->fd();
      if (fd < 0) {
        VLOG(1) << "closed? " << d;
        continue;
      }
      if (!d->wait_readable() && !d->wait_writable()) {
        VLOG(1) << "not waiting? " << fd << " " << d;
        continue;
      }
      waiting_descriptors.push_back(d);
    }

#ifdef _WIN32
    // FD_SETSIZE is very small (64) on Windows.
    // Following is a workaround. i.e. randomly drops descriptors.
    int number_of_fd = 1;
    if (waiting_descriptors.size() >= FD_SETSIZE) {
      GOMA_COUNTERZ("descriptors overcommit");
      std::random_shuffle(waiting_descriptors.begin(),
                          waiting_descriptors.end());
      LOG(INFO) << "#waiting_descriptors is larger than FD_SETSIZE."
                << " #descriptors=" << descriptors.size()
                << " #waiting_descriptors=" << waiting_descriptors.size()
                << " FD_SETSIZE=" << FD_SETSIZE;
    }
#endif
    MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
    FD_SET(fd, &read_fd_);
    MSVC_POP_WARNING();

    for (const auto* d : waiting_descriptors) {
      fd = d->fd();
      bool wait_readable = d->wait_readable();
      bool wait_writable = d->wait_writable();
      CHECK(wait_readable || wait_writable);
#ifndef _WIN32
      CHECK_LT(fd, FD_SETSIZE);
#else
      number_of_fd++;
      if (number_of_fd >= FD_SETSIZE) {
        break;
      }
#endif
      if (wait_readable) {
        if (fd > max_fd_) max_fd_ = fd;
        MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
        FD_SET(fd, &read_fd_);
        MSVC_POP_WARNING();
      }
      if (wait_writable) {
        if (fd > max_fd_) max_fd_ = fd;
        MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
        FD_SET(fd, &write_fd_);
        MSVC_POP_WARNING();
      }
    }
  }

  int PollEventsInternal(absl::Duration timeout) override {
    struct timeval tv = absl::ToTimeval(timeout);
    return select(max_fd_ + 1, &read_fd_, &write_fd_, nullptr, &tv);
  }

  class SelectEventEnumerator : public DescriptorPollerBase::EventEnumerator {
   public:
    SelectEventEnumerator(SelectDescriptorPoller* poller,
                          const DescriptorMap& descriptors)
        : poller_(poller),
          descriptors_(descriptors),
          iter_(descriptors_.begin()),
          current_fd_(-1) {
      DCHECK(poller);
    }

    SocketDescriptor* Next() override {
      // Iterates over descriptors.
      if (iter_ != descriptors_.end()) {
        SocketDescriptor* d = iter_->second.get();
        current_fd_ = d->fd();
        ++iter_;
        return d;
      }
      // Then returns poll_breaker.
      if (current_fd_ != poller_->poll_breaker()->fd()) {
        SocketDescriptor* d = poller_->poll_breaker();
        current_fd_ = d->fd();
        return d;
      }
      return nullptr;
    }

    bool IsReadable() const override {
      return FD_ISSET(current_fd_, &poller_->read_fd_) != 0;
    }
    bool IsWritable() const override {
      return FD_ISSET(current_fd_, &poller_->write_fd_) != 0;
    }

   private:
    SelectDescriptorPoller* poller_;
    const DescriptorMap& descriptors_;
    DescriptorMap::const_iterator iter_;
    int current_fd_;

    DISALLOW_COPY_AND_ASSIGN(SelectEventEnumerator);
  };

  std::unique_ptr<EventEnumerator> GetEventEnumerator(
      const DescriptorMap& descriptors) override {
    return absl::make_unique<SelectEventEnumerator>(this, descriptors);
  }

 private:
  friend class SelectEventEnumerator;
  static absl::once_flag s_init_once_;
  fd_set read_fd_;
  fd_set write_fd_;
  int max_fd_;
  DISALLOW_COPY_AND_ASSIGN(SelectDescriptorPoller);
};

absl::once_flag SelectDescriptorPoller::s_init_once_;

// static
std::unique_ptr<DescriptorPoller> DescriptorPoller::NewDescriptorPoller(
    std::unique_ptr<SocketDescriptor> breaker,
    ScopedSocket&& signaler) {
  return absl::make_unique<SelectDescriptorPoller>(std::move(breaker),
                                                   std::move(signaler));
}

}  // namespace devtools_goma
