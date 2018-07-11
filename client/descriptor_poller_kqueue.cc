// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "descriptor_poller.h"

#include <sys/event.h>
#include <sys/time.h>

#include <unordered_set>

#include "absl/base/call_once.h"
#include "absl/memory/memory.h"
#include "glog/logging.h"
#include "scoped_fd.h"
#include "socket_descriptor.h"

namespace devtools_goma {

class KqueueDescriptorPoller : public DescriptorPollerBase {
 public:
  KqueueDescriptorPoller(std::unique_ptr<SocketDescriptor> breaker,
                         ScopedSocket&& poll_signaler)
      : DescriptorPollerBase(std::move(breaker), std::move(poll_signaler)),
        kqueue_fd_(-1),
        nevents_(0) {
    absl::call_once(s_init_once_, LogDescriptorPollerType);
    kqueue_fd_.reset(kqueue());
    CHECK(kqueue_fd_.valid());
    CHECK(poll_breaker());
    struct kevent kev;
    EV_SET(&kev, poll_breaker()->fd(), EVFILT_READ, EV_ADD, 0, 0, nullptr);
    PCHECK(kevent(kqueue_fd_.fd(), &kev, 1, nullptr, 0, nullptr) != -1)
        << "Cannot add fd for kqueue:" << poll_breaker()->fd();
  }

  static void LogDescriptorPollerType() {
    LOG(INFO) << "descriptor_poller will use \"kqueue\"";
  }

  void RegisterPollEvent(SocketDescriptor* d, EventType type) override {
    DCHECK(d->wait_writable() || d->wait_readable());
    struct kevent kev;
    short filter = 0;
    if (type == kReadEvent) {
      DCHECK(d->wait_readable());
      filter = EVFILT_READ;
    } else if (type == kWriteEvent) {
      DCHECK(d->wait_writable());
      filter = EVFILT_WRITE;
    }
    DCHECK(filter);
    EV_SET(&kev, d->fd(), filter, EV_ADD, 0, 0, nullptr);
    PCHECK(kevent(kqueue_fd_.fd(), &kev, 1, nullptr, 0, nullptr) != -1)
        << "Cannot add fd for kqueue:" << poll_breaker()->fd();
  }

  void UnregisterPollEvent(SocketDescriptor* d, EventType type) override {
    struct kevent kev;
    short filter = (type == kReadEvent) ? EVFILT_READ : EVFILT_WRITE;
    EV_SET(&kev, d->fd(), filter, EV_DELETE, 0, 0, nullptr);
    int r = kevent(kqueue_fd_.fd(), &kev, 1, nullptr, 0, nullptr);
    PCHECK(r != -1 || errno == ENOENT)
        << "Cannot delete fd from kqueue:" << d->fd();
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

    struct kevent kev;
    EV_SET(&kev, d->fd(), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    int r = kevent(kqueue_fd_.fd(), &kev, 1, nullptr, 0, nullptr);
    PCHECK(r != -1 || errno == ENOENT)
        << "Cannot delete fd from kqueue:" << d->fd();

    EV_SET(&kev, d->fd(), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    r = kevent(kqueue_fd_.fd(), &kev, 1, nullptr, 0, nullptr);
    PCHECK(r != -1 || errno == ENOENT)
        << "Cannot delete fd from kqueue:" << d->fd();
  }

 protected:
  void PreparePollEvents(const DescriptorMap& descriptors) override {
    eventlist_.resize(descriptors.size() + 1);
  }

  int PollEventsInternal(int timeout_millisec) override {
    struct timespec tv;
    tv.tv_sec = timeout_millisec / 1000;
    tv.tv_nsec = (timeout_millisec - (tv.tv_sec * 1000)) * 1000000;
    nevents_ = kevent(kqueue_fd_.fd(), nullptr, 0,
        &eventlist_[0], eventlist_.size(), &tv);
    return nevents_;
  }

  class KqueueEventEnumerator : public DescriptorPollerBase::EventEnumerator {
   public:
    KqueueEventEnumerator(KqueueDescriptorPoller* poller,
                          const DescriptorMap& descriptors)
      : poller_(poller),
        descriptors_(descriptors),
        idx_(0),
        current_ev_(nullptr) {
      CHECK(poller_);
      timedout_iter_ = poller_->timeout_waiters_.begin();
    }

    SocketDescriptor* Next() override {
      // Iterates over fired events.
      if (idx_ < poller_->nevents_) {
        current_ev_ = &poller_->eventlist_[idx_++];
        PCHECK(!(current_ev_->flags & EV_ERROR));
        SocketDescriptor* d = nullptr;
        if (static_cast<int>(current_ev_->ident) ==
            poller_->poll_breaker()->fd()) {
          d = poller_->poll_breaker();
        } else {
          DescriptorMap::const_iterator iter = descriptors_.find(
              current_ev_->ident);
          CHECK(iter != descriptors_.end());
          d = iter->second.get();
        }
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
      return current_ev_ && (current_ev_->filter == EVFILT_READ);
    }
    bool IsWritable() const override {
      return current_ev_ && (current_ev_->filter == EVFILT_WRITE);
    }

  private:
    KqueueDescriptorPoller* poller_;
    const DescriptorMap& descriptors_;
    int idx_;
    struct kevent* current_ev_;
    std::unordered_set<SocketDescriptor*>::const_iterator timedout_iter_;
    std::unordered_set<SocketDescriptor*> event_received_;

    DISALLOW_COPY_AND_ASSIGN(KqueueEventEnumerator);
  };

  std::unique_ptr<EventEnumerator> GetEventEnumerator(
      const DescriptorMap& descriptors) override {
    DCHECK(nevents_ <= static_cast<int>(eventlist_.size()));
    return absl::make_unique<KqueueEventEnumerator>(this, descriptors);
  }

 private:
  friend class KqueueEventEnumerator;
  static absl::once_flag s_init_once_;
  ScopedFd kqueue_fd_;
  std::vector<struct kevent> eventlist_;
  std::unordered_set<SocketDescriptor*> timeout_waiters_;
  int nevents_;
  DISALLOW_COPY_AND_ASSIGN(KqueueDescriptorPoller);
};

absl::once_flag KqueueDescriptorPoller::s_init_once_;

// static
std::unique_ptr<DescriptorPoller> DescriptorPoller::NewDescriptorPoller(
    std::unique_ptr<SocketDescriptor> breaker,
    ScopedSocket&& signaler) {
  return absl::make_unique<KqueueDescriptorPoller>(std::move(breaker),
                                                   std::move(signaler));
}

}  // namespace devtools_goma
