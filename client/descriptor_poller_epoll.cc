// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifdef USE_EPOLL

#include "descriptor_poller.h"

#include <memory>
#include <unordered_set>

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
# error kernel is too old to use epoll. Try "make USE_SELECT=1".
#endif
#include <sys/epoll.h>
#define EPOLL_SIZE_HINT FD_SETSIZE  // Any value but not 0 should be ok.

#include "absl/memory/memory.h"
#include "compiler_specific.h"
#include "glog/logging.h"
#include "scoped_fd.h"
#include "socket_descriptor.h"

namespace devtools_goma {

class EpollDescriptorPoller : public DescriptorPollerBase {
 public:
  EpollDescriptorPoller(std::unique_ptr<SocketDescriptor> breaker,
                        ScopedSocket&& poll_signaler)
      : DescriptorPollerBase(std::move(breaker), std::move(poll_signaler)),
        epoll_fd_(-1),
        nevents_(0),
        last_nevents_(0) {
    epoll_fd_.reset(epoll_create(EPOLL_SIZE_HINT));
    CHECK(epoll_fd_.valid());
    CHECK(poll_breaker());
    struct epoll_event ev = {};
    ev.events = EPOLLIN;
    ev.data.ptr = poll_breaker();
    PCHECK(epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_ADD, poll_breaker()->fd(),
                     &ev) != -1);
  }

  void RegisterPollEvent(SocketDescriptor* d, EventType type) override {
    DCHECK(d->wait_writable() || d->wait_readable());
    struct epoll_event ev = {};
    ev.data.ptr = d;
    if (type == kReadEvent || d->wait_readable()) {
      DCHECK(d->wait_readable());
      ev.events |= EPOLLIN;
    }
    if (type == kWriteEvent || d->wait_writable()) {
      DCHECK(d->wait_writable());
      ev.events |= EPOLLOUT;
    }
    int r = epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_ADD, d->fd(), &ev);
    if (r < 0 && errno == EEXIST) {
      r = epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_MOD, d->fd(), &ev);
    }
    PCHECK(r != -1) << "Cannot add fd for epoll:" << d->fd();
  }

  void UnregisterPollEvent(SocketDescriptor* d,
                           EventType type ALLOW_UNUSED) override {
    struct epoll_event ev = {};
    ev.data.ptr = d;
    int op = EPOLL_CTL_DEL;
    if (d->wait_readable()) {
      ev.events |= EPOLLIN;
      op = EPOLL_CTL_MOD;
    }
    if (d->wait_writable()) {
      ev.events |= EPOLLOUT;
      op = EPOLL_CTL_MOD;
    }
    PCHECK(epoll_ctl(epoll_fd_.fd(), op, d->fd(), &ev) != -1)
        << "Cannot delete fd for epoll:" << d->fd();
  }

  void RegisterTimeoutEvent(SocketDescriptor* d) override {
    timeout_waiters_.insert(d);
  }

  void UnregisterTimeoutEvent(SocketDescriptor* d) override {
    timeout_waiters_.erase(d);
  }

  void UnregisterDescriptor(SocketDescriptor* d) override {
    CHECK(d);
    timeout_waiters_.erase(d);
    int r = epoll_ctl(epoll_fd_.fd(), EPOLL_CTL_DEL, d->fd(), nullptr);
    PCHECK(r != -1 || errno == ENOENT)
        << "Cannot delete fd for epoll:" << d->fd();
  }

 protected:
  void PreparePollEvents(const DescriptorMap& descriptors) override {
    nevents_ = descriptors.size() + 1;
    if (last_nevents_ < nevents_) {
      events_ = absl::make_unique<struct epoll_event[]>(nevents_);
    }
    last_nevents_ = nevents_;
  }

  int PollEventsInternal(int timeout_millisec) override {
    nfds_ = epoll_wait(epoll_fd_.fd(), events_.get(),
                       nevents_, timeout_millisec);
    return nfds_;
  }

  class EpollEventEnumerator : public DescriptorPollerBase::EventEnumerator {
   public:
    explicit EpollEventEnumerator(EpollDescriptorPoller* poller)
        : poller_(poller), idx_(0), current_ev_(nullptr) {
      CHECK(poller_);
      timedout_iter_ = poller_->timeout_waiters_.begin();
    }

    SocketDescriptor* Next() override {
      // Iterates over fired events.
      if (idx_ < poller_->nfds_) {
        current_ev_ = &poller_->events_.get()[idx_++];
        SocketDescriptor* d = static_cast<SocketDescriptor*>(
            current_ev_->data.ptr);
        event_received_.insert(d);
        return d;
      }
      current_ev_ = nullptr;
      // Then iterates over timed out ones.
      for (; timedout_iter_ != poller_->timeout_waiters_.end();
        ++timedout_iter_) {
          if (event_received_.find(*timedout_iter_) == event_received_.end())
            return *timedout_iter_++;
      }
      return nullptr;
    }

    bool IsReadable() const override {
      return current_ev_ && (current_ev_->events & EPOLLIN);
    }
    bool IsWritable() const override {
      return current_ev_ && (current_ev_->events & EPOLLOUT);
    }

  private:
    EpollDescriptorPoller* poller_;
    int idx_;
    struct epoll_event* current_ev_;
    std::unordered_set<SocketDescriptor*>::const_iterator timedout_iter_;
    std::unordered_set<SocketDescriptor*> event_received_;

    DISALLOW_COPY_AND_ASSIGN(EpollEventEnumerator);
  };

  std::unique_ptr<EventEnumerator> GetEventEnumerator(
      const DescriptorMap& descriptors ALLOW_UNUSED) override {
    return absl::make_unique<EpollEventEnumerator>(this);
  }

 private:
  friend class EpollEventEnumerator;
  ScopedFd epoll_fd_;
  std::unique_ptr<struct epoll_event[]> events_;
  std::unordered_set<SocketDescriptor*> timeout_waiters_;
  int nevents_;
  int last_nevents_;
  int nfds_;
  DISALLOW_COPY_AND_ASSIGN(EpollDescriptorPoller);
};

// static
std::unique_ptr<DescriptorPoller> DescriptorPoller::NewDescriptorPoller(
    std::unique_ptr<SocketDescriptor> breaker,
    ScopedSocket&& signaler) {
  return absl::make_unique<EpollDescriptorPoller>(std::move(breaker),
                                                  std::move(signaler));
}

}  // namespace devtools_goma

#endif  // USE_EPOLL
