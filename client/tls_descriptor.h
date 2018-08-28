// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_TLS_DESCRIPTOR_H_
#define DEVTOOLS_GOMA_CLIENT_TLS_DESCRIPTOR_H_

#include <memory>

#include "basictypes.h"
#include "descriptor.h"
#include "http_util.h"
#include "tls_engine.h"
#include "worker_thread.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

class Closure;
class SocketDescriptor;

class TLSDescriptor : public Descriptor {
 public:
  struct Options {
    Options() : use_proxy(false) {}
    string dest_host_name;
    int dest_port;
    bool use_proxy;
  };
  // It doesn't take ownership of e and wm.
  // It keep desc inside but desc should be deleted by WorkerThreadManager.
  TLSDescriptor(SocketDescriptor* desc,
                TLSEngine* e,
                Options options,
                WorkerThreadManager* wm);
  ~TLSDescriptor() override;

  // To be deleted by WorkerThreadManager.
  SocketDescriptor* socket_descriptor() override { return socket_descriptor_; }

  void NotifyWhenReadable(std::unique_ptr<PermanentClosure> closure) override;
  void NotifyWhenWritable(std::unique_ptr<PermanentClosure> closure) override;
  void ClearWritable() override;
  void NotifyWhenTimedout(absl::Duration timeout,
                          OneshotClosure* closure) override;
  void ChangeTimeout(absl::Duration timeout) override;
  ssize_t Read(void* ptr, size_t len) override;
  ssize_t Write(const void* ptr, size_t len) override;

  bool NeedRetry() const override;
  bool CanReuse() const override;
  // TODO: implement the same feature with shutdown(2) for SSL.
  string GetLastErrorMessage() const override;
  void StopRead() override;
  void StopWrite() override;

  void Init();

 private:
  void PutClosuresInRunQueue() const;

  // TransportLayerReadable/Writable are called back when a socket get ready.
  void TransportLayerReadable();
  void TransportLayerWritable();

  // Suspend to wait writable.
  void SuspendTransportWritable();
  void ResumeTransportWritable();

  // Stop / restart notification from transport layer.
  void StopTransportLayer();
  void RestartTransportLayer();

  // HTTP request to ask proxy to connect the server.
  string CreateProxyRequestMessage();

  SocketDescriptor* socket_descriptor_;
  TLSEngine* engine_;

  WorkerThreadManager* wm_;
  WorkerThread::ThreadId thread_;
  std::unique_ptr<PermanentClosure> readable_closure_;
  std::unique_ptr<PermanentClosure> writable_closure_;
  char network_read_buffer_[kNetworkBufSize];
  string network_write_buffer_;
  size_t network_write_offset_;
  // Shows application read/write failed because TLS engine needs more work.
  bool ssl_pending_;
  // Shows readable_closure_ can be callable.
  bool active_read_;
  // Shows writable_closure_ can be callable.
  bool active_write_;
  // Shows transport layer communication failed.
  bool io_failed_;

  // HTTP proxy related paramters.
  const Options options_;
  enum ConnectStatus { NEED_WRITE, NEED_READ, READY} connect_status_;
  // Shows underlying SocketDescriptor closed.
  bool is_closed_;
  string proxy_response_;
  // Only used if transport layer socket is closed but we need to keep
  // http.cc read TLSDescriptor.  (b/22515030)
  // In such situation we need to let HttpClient::Task::DoRead to read
  // TLSDescriptor but at the same time, we need to allow it to stop
  // TLSDescriptor.  If TLSDescriptor is stopped, this wrapper is disabled
  // not to run readable closure.
  WorkerThread::CancelableClosure* cancel_readable_closure_;
  DISALLOW_COPY_AND_ASSIGN(TLSDescriptor);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TLS_DESCRIPTOR_H_
