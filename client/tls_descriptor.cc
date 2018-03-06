// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "tls_descriptor.h"

#include <sstream>

#include "callback.h"
#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "socket_descriptor.h"
#include "glog/logging.h"
#include "ioutil.h"

namespace devtools_goma {

TLSDescriptor::TLSDescriptor(
    SocketDescriptor* desc, TLSEngine* e,
    const Options& options,
    WorkerThreadManager* wm)
    : d_(desc), engine_(e), wm_(wm),
      readable_closure_(nullptr), writable_closure_(nullptr),
      network_write_offset_(0), ssl_pending_(false),
      active_read_(false), active_write_(false),
      io_failed_(false),
      options_(options), connect_status_(READY), is_closed_(false),
      cancel_readable_closure_(nullptr) {
  thread_ = GetCurrentThreadId();
}

TLSDescriptor::~TLSDescriptor() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  if (cancel_readable_closure_) {
    cancel_readable_closure_->Cancel();
    cancel_readable_closure_ = nullptr;
  }
}

void TLSDescriptor::Init() {
  if (options_.use_proxy && !engine_->IsRecycled())
    connect_status_ = NEED_WRITE;

  d_->NotifyWhenReadable(
      NewPermanentCallback(this, &TLSDescriptor::TransportLayerReadable));
  d_->NotifyWhenWritable(
      NewPermanentCallback(this, &TLSDescriptor::TransportLayerWritable));
}

void TLSDescriptor::NotifyWhenReadable(
    std::unique_ptr<PermanentClosure> closure) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  readable_closure_ = std::move(closure);
  active_read_ = true;
  RestartTransportLayer();
  VLOG(1) << "Notify when " << d_->fd()
          << " readable " << readable_closure_.get();
}

void TLSDescriptor::NotifyWhenWritable(
    std::unique_ptr<PermanentClosure> closure) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  writable_closure_ = std::move(closure);
  active_write_ = true;
  RestartTransportLayer();
  VLOG(1) << "Notify when " << d_->fd()
          << " writable " << writable_closure_.get();
}

void TLSDescriptor::ClearWritable() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  VLOG(1) << "Clear " << d_->fd() << " writable " << writable_closure_.get();
  active_write_ = false;
  writable_closure_.reset();
}

void TLSDescriptor::NotifyWhenTimedout(double timeout,
                                       OneshotClosure* closure) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  d_->NotifyWhenTimedout(timeout, closure);
}

void TLSDescriptor::ChangeTimeout(double timeout) {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  // once is_closed_, timeout closure is cleared (in StopTransportLayer)
  if (is_closed_)
    return;
  d_->ChangeTimeout(timeout);
}

ssize_t TLSDescriptor::Read(void* ptr, size_t len) {
  cancel_readable_closure_ = nullptr;
  if (io_failed_)
    return -1;
  // It seems stack if we do not restart transport layer communications.
  // It might be because TLS may send something like ACK, we guess.
  ResumeTransportWritable();

  const int ret = engine_->Read(ptr, len);
  if (ret == TLSEngine::TLS_WANT_READ || ret == TLSEngine::TLS_WANT_WRITE) {
    if (is_closed_) {
      LOG(INFO) << "socket has already been closed by peer: fd=" << d_->fd();
      return 0;
    }
    ssl_pending_ = true;
  } else if (ret < 0) {  // TLSEngine error except want read/write.
    LOG(ERROR) << "Error occured during application read.";
  } else {
    ssl_pending_ = false;
  }
  if (is_closed_ && ret > 0) {
    // Make readable_closure_ read all available data.
    DCHECK(readable_closure_.get());
    cancel_readable_closure_ = wm_->RunDelayedClosureInThread(
        FROM_HERE, thread_, 0,
        NewCallback(static_cast<Closure*>(readable_closure_.get()),
                    &Closure::Run));
  }
  return ret;
}

ssize_t TLSDescriptor::Write(const void* ptr, size_t len) {
  if (io_failed_ || is_closed_)
    return -1;
  ResumeTransportWritable();
  const int ret = engine_->Write(ptr, len);
  if (ret == TLSEngine::TLS_WANT_READ || ret == TLSEngine::TLS_WANT_WRITE) {
    ssl_pending_ = true;
  } else if (ret < 0) {  // TLSEngine error except want read/write.
    LOG(ERROR) << "Error occured during application write.";
  } else {
    ssl_pending_ = false;
  }
  return ret;
}

bool TLSDescriptor::NeedRetry() const {
  // TLS engine will not get interrupted but view from application side
  // should be similar.
  return ssl_pending_ && !io_failed_ && !is_closed_;
}

string TLSDescriptor::GetLastErrorMessage() const {
  string err_msg = d_->GetLastErrorMessage();
  if (!err_msg.empty())
    err_msg.append(" ,");
  return err_msg + "TLS engine:" + engine_->GetLastErrorMessage();
}

void TLSDescriptor::StopRead() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  active_read_ = false;
  if (!active_write_ && !ssl_pending_) {
    StopTransportLayer();
  }
  if (cancel_readable_closure_) {
    cancel_readable_closure_->Cancel();
    cancel_readable_closure_ = nullptr;
  }
}

void TLSDescriptor::StopWrite() {
  DCHECK(THREAD_ID_IS_SELF(thread_));
  active_write_ = false;
  if (!active_read_ && !ssl_pending_) {
    StopTransportLayer();
  }
}

void TLSDescriptor::TransportLayerReadable() {
  size_t read_size = std::min(engine_->GetBufSizeFromTransport(),
                              sizeof(network_read_buffer_));
  const ssize_t read_bytes = d_->Read(network_read_buffer_, read_size);
  if (read_bytes < 0 && d_->NeedRetry())
      return;

  if (read_bytes == 0) {  // EOF.
    LOG(INFO) << "Remote closed. "
              << " fd=" << d_->fd()
              << " ret=" << read_bytes
              << " err=" << d_->GetLastErrorMessage();
    is_closed_ = true;
    StopTransportLayer();
    PutClosuresInRunQueue();
    return;
  }
  if (read_bytes < 0) {  // error.
    LOG(WARNING) << "Transport layer read " << d_->fd() << " failed."
                 << " ret=" << read_bytes
                 << " err=" << d_->GetLastErrorMessage();
    StopTransportLayer();
    io_failed_ = true;
    PutClosuresInRunQueue();
    return;
  } else if (connect_status_ == READY) {
    int ret = engine_->SetDataFromTransport(
        absl::string_view(network_read_buffer_, read_bytes));
    if (ret < 0) {  // Error in TLS engine.
      StopTransportLayer();
      io_failed_ = true;
      PutClosuresInRunQueue();
      return;
    }
    CHECK_EQ(ret, static_cast<int>(read_bytes));

    ResumeTransportWritable();
    if (!engine_->IsIOPending()) {
      PutClosuresInRunQueue();
      return;
    }
  } else if (connect_status_ == NEED_READ) {
    int status_code = 0;
    size_t offset;
    size_t content_length;
    proxy_response_.append(network_read_buffer_, read_bytes);
    if (ParseHttpResponse(proxy_response_, &status_code, &offset,
                          &content_length, nullptr)) {
      if (status_code / 100 == 2) {
        connect_status_ = READY;
        ResumeTransportWritable();
      } else {
        LOG(ERROR) << "Proxy's status code != 2xx."
                   << " Details:" << proxy_response_;
        StopTransportLayer();
        io_failed_ = true;
        PutClosuresInRunQueue();
      }
    }
  } else if (connect_status_ == NEED_WRITE) {
    LOG(ERROR) << "Unexpected read occured when waiting writable."
               << "buf:" << absl::string_view(network_read_buffer_, read_bytes);
  }
}

void TLSDescriptor::TransportLayerWritable() {

  if (network_write_buffer_.empty()) {
    if (connect_status_ == READY)
      CHECK_GE(engine_->GetDataToSendTransport(&network_write_buffer_), 0);
    else if (connect_status_ == NEED_WRITE)
      network_write_buffer_ = CreateProxyRequestMessage();
    network_write_offset_ = 0;
    if (network_write_buffer_.size() == 0)
      SuspendTransportWritable();
    if (!engine_->IsIOPending()) {
      PutClosuresInRunQueue();
      return;
    }
  }

  if (network_write_buffer_.size() - network_write_offset_ > 0) {
    int ret = d_->Write(network_write_buffer_.c_str() + network_write_offset_,
                        network_write_buffer_.size() - network_write_offset_);
    if (ret < 0 && d_->NeedRetry())
      return;
    if (ret <= 0) {
      LOG(WARNING) << "Transport layer write " << d_->fd() << " failed."
                   << " ret=" << ret
                   << " err=" << d_->GetLastErrorMessage();
      StopTransportLayer();
      io_failed_ = true;
      PutClosuresInRunQueue();
      return;
    } else {
      network_write_offset_ += ret;
      if (network_write_buffer_.size() == network_write_offset_) {
        network_write_buffer_.clear();
        network_write_offset_ = 0;
        if (connect_status_ == NEED_WRITE)
          connect_status_ = NEED_READ;
      }
    }
  }
}

void TLSDescriptor::PutClosuresInRunQueue() const {
  // TODO: check readable/writeble of data if possible.
  // Since SSL_pending seems not works well with BIO pair, we cannot check
  // readable.  I could not find a good function to check it writable.
  bool set_callback = false;
  if (active_write_ && writable_closure_.get() != nullptr) {
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_, writable_closure_.get(),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
    set_callback = true;
  }

  if (active_read_ && readable_closure_.get() != nullptr) {
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_, readable_closure_.get(),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
    set_callback = true;
  }
  LOG_IF(ERROR, !set_callback)
    << "PutClosuresInRunQueue actually did nothing. "
    << "We expect control goes back to the user of this libary."
    << " active_write=" << active_write_
    << " writable_closure=" << (writable_closure_ != nullptr)
    << " active_read=" << active_read_
    << " readable_closure" << (readable_closure_ != nullptr)
    << " is_closed=" << is_closed_
    << " io_failed=" << io_failed_;
}

void TLSDescriptor::SuspendTransportWritable() {
  d_->StopWrite();
  d_->UnregisterWritable();
}

void TLSDescriptor::ResumeTransportWritable() {
  if (is_closed_) {
    LOG(INFO) << "socket has already been closed: fd=" << d_->fd();
    return;
  }
  d_->RestartWrite();
}

void TLSDescriptor::StopTransportLayer() {
  d_->StopRead();
  d_->StopWrite();
  if (is_closed_) {
    d_->ClearTimeout();
  }
}

void TLSDescriptor::RestartTransportLayer() {
  if (is_closed_) {
    LOG(INFO) << "socket has already been closed: fd=" << d_->fd();
    return;
  }
  d_->RestartRead();
  d_->RestartWrite();
}

string TLSDescriptor::CreateProxyRequestMessage() {
  std::ostringstream http_send_message;
  std::ostringstream dest_host_port;
  dest_host_port << options_.dest_host_name << ":" << options_.dest_port;
  http_send_message << "CONNECT " << dest_host_port.str() << " HTTP/1.1\r\n";
  http_send_message << "Host: " << dest_host_port.str() << "\r\n";
  http_send_message << "UserAgent: " << kUserAgentString << "\r\n";
  http_send_message << "\r\n";
  return http_send_message.str();
}

bool TLSDescriptor::CanReuse() const {
  return !is_closed_ && !io_failed_ && d_->CanReuse();
}

}  // namespace devtools_goma
