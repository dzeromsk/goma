// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "fake_tls_engine.h"

#include <gtest/gtest.h>

namespace devtools_goma {

FakeTLSEngine::~FakeTLSEngine() {
  if (broken_ != FAKE_TLS_NO_BROKEN)
    EXPECT_TRUE(execute_broken_);
}

bool FakeTLSEngine::IsIOPending() const {
  // Nothing is pending.
  return false;
}

bool FakeTLSEngine::IsReady() const {
  return true;
}

int FakeTLSEngine::GetDataToSendTransport(string *data) {
  if (broken_ == FAKE_TLS_GET_BROKEN) {
    execute_broken_ = true;
    return TLSEngine::TLS_ERROR;
  }
  data->clear();
  data->append(buffer_app_to_sock_);
  buffer_app_to_sock_.clear();
  return data->size();
}

size_t FakeTLSEngine::GetBufSizeFromTransport() {
  return 1024;
}

int FakeTLSEngine::SetDataFromTransport(const absl::string_view& data) {
  if (broken_ == FAKE_TLS_SET_BROKEN) {
    execute_broken_ = true;
    return TLSEngine::TLS_ERROR;
  }
  buffer_sock_to_app_.append(string(data));
  return data.size();
}

int FakeTLSEngine::Read(void* data, int size) {
  if (broken_ == FAKE_TLS_READ_BROKEN) {
    execute_broken_ = true;
    return TLSEngine::TLS_ERROR;
  }
  if (buffer_sock_to_app_.size() == 0)
    return TLSEngine::TLS_WANT_READ;
  int copy_size = buffer_sock_to_app_.size() - offset_sock_to_app_;
  if (max_read_size_ > 0 && copy_size > max_read_size_)
    copy_size = max_read_size_;
  if (size < copy_size)
    copy_size = size;
  if (copy_size > 0)
  memmove(data, buffer_sock_to_app_.c_str() + offset_sock_to_app_, copy_size);
  offset_sock_to_app_ += copy_size;
  if (buffer_sock_to_app_.size() == offset_sock_to_app_) {
    buffer_sock_to_app_.clear();
    offset_sock_to_app_ = 0;
  }
  return copy_size;
}

int FakeTLSEngine::Write(const void* data, int size) {
  if (broken_ == FAKE_TLS_WRITE_BROKEN) {
    execute_broken_ = true;
    return TLSEngine::TLS_ERROR;
  }
  buffer_app_to_sock_.append(string(static_cast<const char*>(data), size));
  return size;
}

FakeTLSEngineFactory::~FakeTLSEngineFactory() {
  EXPECT_EQ(sock_, -1);
  EXPECT_FALSE(tls_engine_);
}

TLSEngine* FakeTLSEngineFactory::NewTLSEngine(int sock) {
  if (sock_ == -1) {
    sock_ = sock;
    tls_engine_ = new FakeTLSEngine;
    tls_engine_->SetBroken(broken_);
    tls_engine_->SetMaxReadSize(max_read_size_);
  }

  // We should implement more powerful mock if you use more than one socket.
  EXPECT_EQ(sock, sock_);
  return tls_engine_;
}

void FakeTLSEngineFactory::WillCloseSocket(int sock) {
  EXPECT_NE(sock, -1);
  EXPECT_EQ(sock, sock_);
  delete tls_engine_;
  sock_ = -1;
  tls_engine_ = nullptr;
}

}  // namespace devtools_goma
