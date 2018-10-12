// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SOCKET_DESCRIPTOR_H_
#define DEVTOOLS_GOMA_CLIENT_SOCKET_DESCRIPTOR_H_

#include <memory>

#include "absl/types/optional.h"
#include "basictypes.h"
#include "descriptor.h"
#include "scoped_fd.h"
#include "worker_thread.h"

namespace devtools_goma {

class SocketDescriptor : public Descriptor {
 public:
  SocketDescriptor(ScopedSocket&& fd,
                   WorkerThread::Priority priority,
                   WorkerThread* worker);
  ~SocketDescriptor() override;

  virtual int fd() const { return fd_.get(); }
  virtual const IOChannel* wrapper() const { return &fd_; }
  ScopedSocket ReleaseFd() { return std::move(fd_); }
  virtual WorkerThread::Priority priority() const { return priority_; }
  SocketDescriptor* socket_descriptor() override { return this; }
  // closure must be permanent closure.
  void NotifyWhenReadable(
      std::unique_ptr<PermanentClosure> closure) override;
  // closure must be permanent closure.
  void NotifyWhenWritable(
      std::unique_ptr<PermanentClosure> closure) override;
  virtual void ClearReadable();
  void ClearWritable() override;
  // closure must be one-shot closure.
  void NotifyWhenTimedout(absl::Duration timeout,
                          OneshotClosure* closure) override;
  void ChangeTimeout(absl::Duration timeout) override;
  virtual void ClearTimeout();
  ssize_t Read(void* ptr, size_t len) override;
  ssize_t Write(const void* ptr, size_t len) override;

  bool NeedRetry() const override;
  virtual int ShutdownForSend();
  string GetLastErrorMessage() const override { return last_error_message_; }
  virtual bool IsReadable() const;
  bool IsClosed() const { return is_closed_; }
  bool CanReuse() const override {
    return !IsClosed() && last_error_message_.empty();
  }
  void StopRead() override;
  void StopWrite() override;
  virtual void RestartRead();
  virtual void RestartWrite();
  void UnregisterWritable();

  bool wait_readable() const;
  bool wait_writable() const;

  OneshotClosure* GetReadableClosure();
  OneshotClosure* GetWritableClosure();
  OneshotClosure* GetTimeoutClosure();

  string PeerName() const;

 private:
  // Gets a one-shot closure to run permanent "closure" and
  // mark a closure in run queue,  so that it won't add new closure in run
  // queue while the closure is waiting to run.
  // If the closure is not permanent closure, make sure delete closure when
  // GetClosure() returns nullptr, or check *in_queue is false before calling
  // GetClosure().
  OneshotClosure* GetClosure(bool* in_queue, bool* active,
                             PermanentClosure* closure);

  // Marks no closure in run queue and runs permanent "closure".
  // If not active_, it doesn't run closure.
  void RunCallback(PermanentClosure* closure, bool* in_queue, bool* active);

  // Fires timeout.
  // If read or write closure in queue while this closure has been pending
  // in queue, cancel timeout.
  void TimeoutClosure();

  void UpdateLastErrorStatus();

  ScopedSocket fd_;
  const WorkerThread::Priority priority_;
  WorkerThread* worker_;
  // permanent closure.
  std::unique_ptr<PermanentClosure> readable_closure_;
  // permanent closure.
  std::unique_ptr<PermanentClosure> writable_closure_;
  absl::optional<absl::Duration> timeout_;
  WorkerThread::Timestamp last_time_;

  // permanent to TimeoutClosure()
  std::unique_ptr<PermanentClosure> timeout_run_closure_;
  // single shot specified by NotifyWhenTimeout.
  std::unique_ptr<OneshotClosure> timeout_closure_;
  WorkerThread::ThreadId thread_;
  bool read_in_queue_;
  bool write_in_queue_;
  bool timeout_in_queue_;
  bool active_read_;
  bool active_write_;
  string last_error_message_;
  bool write_poll_registered_;
  bool is_closed_;
  bool need_retry_;
  DISALLOW_COPY_AND_ASSIGN(SocketDescriptor);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SOCKET_DESCRIPTOR_H_
