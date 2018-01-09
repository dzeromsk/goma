// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FAKE_TLS_ENGINE_H_
#define DEVTOOLS_GOMA_CLIENT_FAKE_TLS_ENGINE_H_

#include "compiler_specific.h"
#include "tls_engine.h"

namespace devtools_goma {

// This just pass through transport input to application, and vice-versa.
// That is why this is called "fake".
class FakeTLSEngine : public TLSEngine {
 public:
  enum FakeTLSEngineBroken {
    FAKE_TLS_NO_BROKEN = 0,
    FAKE_TLS_GET_BROKEN = 1,
    FAKE_TLS_SET_BROKEN = 2,
    FAKE_TLS_READ_BROKEN = 3,
    FAKE_TLS_WRITE_BROKEN = 4,
  };
  bool IsIOPending() const override;

  int GetDataToSendTransport(string* data) override;
  size_t GetBufSizeFromTransport() override;
  int SetDataFromTransport(const StringPiece& data) override;

  // Read and Write return number of read/write bytes if success.
  // Otherwise, TLSErrorReason.
  int Read(void* data, int size) override;
  int Write(const void* data, int size) override;

  string GetLastErrorMessage() const override {
    return "TLSEngine error message";
  }

  bool IsRecycled() const override { return is_recycled_; }

 protected:
  friend class FakeTLSEngineFactory;
  FakeTLSEngine() :
    offset_sock_to_app_(0),
    is_recycled_(false),
    broken_(FAKE_TLS_NO_BROKEN),
    execute_broken_(false),
    max_read_size_(-1) {}
  ~FakeTLSEngine() override;
  virtual void SetIsRecycled(bool value) { is_recycled_ = value; }
  virtual void SetBroken(FakeTLSEngineBroken broken) { broken_ = broken; }
  virtual void SetMaxReadSize(int size) { max_read_size_ = size; }

 private:
  string buffer_app_to_sock_;
  string buffer_sock_to_app_;
  size_t offset_sock_to_app_;
  bool is_recycled_;
  enum FakeTLSEngineBroken broken_;
  bool execute_broken_;
  int max_read_size_;

  DISALLOW_COPY_AND_ASSIGN(FakeTLSEngine);
};

// TLSEngineFactory is synchronized.
class FakeTLSEngineFactory : public TLSEngineFactory {
 public:
  FakeTLSEngineFactory() :
    sock_(-1), tls_engine_(NULL), broken_(FakeTLSEngine::FAKE_TLS_NO_BROKEN),
    max_read_size_(-1) {}
  ~FakeTLSEngineFactory() override;
  TLSEngine* NewTLSEngine(int sock) override;
  void WillCloseSocket(int sock) override;

  string GetCertsInfo() override { return certs_info_; }
  void SetBroken(FakeTLSEngine::FakeTLSEngineBroken broken) {
    broken_ = broken;
  }
  void SetMaxReadSize(int size) {
    max_read_size_ = size;
  }
  // Dummy.
  void SetHostname(const string& hostname ALLOW_UNUSED) override {}

 private:
  int sock_;
  FakeTLSEngine* tls_engine_;
  string certs_info_;
  enum FakeTLSEngine::FakeTLSEngineBroken broken_;
  int max_read_size_;

  DISALLOW_COPY_AND_ASSIGN(FakeTLSEngineFactory);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FAKE_TLS_ENGINE_H_
