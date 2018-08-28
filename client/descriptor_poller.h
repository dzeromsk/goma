// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_DESCRIPTOR_POLLER_H_
#define DEVTOOLS_GOMA_CLIENT_DESCRIPTOR_POLLER_H_

#include <deque>
#include <map>
#include <memory>

#include "absl/time/time.h"
#include "descriptor_event_type.h"
#include "scoped_fd.h"
#include "worker_thread.h"

namespace devtools_goma {

class AutoLockStat;
class SocketDescriptor;

class DescriptorPoller {
 public:
  using EventType = DescriptorEventType;

  typedef std::map<WorkerThread::Priority, std::deque<OneshotClosure*>>
      CallbackQueue;
  typedef std::map<int, std::unique_ptr<SocketDescriptor>> DescriptorMap;

  // Creates a new DescriptorPoller instance.
  // |poll_breaker| is a special Descriptor that has no callbacks and is
  // only used to break the PollEvents.
  // poll_signaler should not be SocketDescriptor because it will be used
  // on other thread than the thread for the DescriptorPoller.
  static std::unique_ptr<DescriptorPoller> NewDescriptorPoller(
      std::unique_ptr<SocketDescriptor> poll_breaker,
      ScopedSocket&& poll_signaler);
  DescriptorPoller() {}
  virtual ~DescriptorPoller() {}

  // Registers and unregister polling event for a given descriptor.
  // They may be called on a different thread (with lock) from the one
  // polling events.
  virtual void RegisterPollEvent(SocketDescriptor* d, EventType) = 0;
  virtual void UnregisterPollEvent(SocketDescriptor* d, EventType) = 0;
  virtual void RegisterTimeoutEvent(SocketDescriptor* d) = 0;
  virtual void UnregisterTimeoutEvent(SocketDescriptor* d) = 0;
  virtual void UnregisterDescriptor(SocketDescriptor* d) = 0;

  // Blocking; polls events over descriptors at most |timeout| and populates
  // |callbacks| if any descriptors which has higher priority than |priority|
  // had any events.
  // This must be called with |lock| locked and on a single polling thread.
  // Returns true if poll breakers broke poller.
  virtual bool PollEvents(const DescriptorMap& descriptors,
                          absl::Duration timeout,
                          int priority,
                          CallbackQueue* callbacks,
                          Lock* lock,
                          AutoLockStat** statp) = 0;

  virtual void Signal() = 0;

 private:
  DISALLOW_COPY_AND_ASSIGN(DescriptorPoller);
};

class DescriptorPollerBase : public DescriptorPoller {
 public:
  DescriptorPollerBase(std::unique_ptr<SocketDescriptor> poll_breaker,
                       ScopedSocket&& poll_signaler);
  ~DescriptorPollerBase() override {}

  class EventEnumerator {
   public:
    // Returns the next descriptor on which events have occured.
    // Returns NULL if there're no more descriptors.
    virtual SocketDescriptor* Next() = 0;

    // Returns the current descriptor's information.
    virtual bool IsReadable() const = 0;
    virtual bool IsWritable() const = 0;

    virtual ~EventEnumerator() {}
  };

  // Returns true if idle.
  bool PollEvents(const DescriptorMap& descriptors,
                  absl::Duration timeout,
                  int priority,
                  CallbackQueue* callbacks,
                  Lock* lock,
                  AutoLockStat** statp) override;
  void Signal() override;

 protected:
  // Called right before PollEventsInternal; with lock held.
  // Scans descriptors or examines registered descriptors to determine
  // which descriptors to be polled.
  virtual void PreparePollEvents(const DescriptorMap& descriptors) = 0;

  // Does actual polling.  Returns the number of file descriptors ready
  // for the requested I/O, zero if it has timed out, or -1 on failure.
  virtual int PollEventsInternal(absl::Duration timeout) = 0;

  // Called right after PollEventsInternal; with lock held.
  // Returns EventEnumerator with which caller can iterate over descriptors
  // that have had any events.
  virtual std::unique_ptr<EventEnumerator> GetEventEnumerator(
      const DescriptorMap& descriptors) = 0;

  SocketDescriptor* poll_breaker() const { return poll_breaker_.get(); }

 private:
  std::unique_ptr<SocketDescriptor> poll_breaker_;
  ScopedSocket poll_signaler_;
  WorkerThread::ThreadId poll_thread_;
  DISALLOW_COPY_AND_ASSIGN(DescriptorPollerBase);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_DESCRIPTOR_POLLER_H_
