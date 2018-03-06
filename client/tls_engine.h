// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// TLSEngine is an interface of Transport Layer Security (TLS) engine class.
// This is a middle man between application and socket.
// See: Example in http://www.openssl.org/docs/crypto/BIO_s_bio.html
//
// TLSEngineFactory is an interface of TLSEngine factory class.
// Returns the TLSEngine that matches a socket descriptor.
// If the socket descriptor is new, TLSEngine instance is created.

#ifndef DEVTOOLS_GOMA_CLIENT_TLS_ENGINE_H_
#define DEVTOOLS_GOMA_CLIENT_TLS_ENGINE_H_

#include <string>

#include "absl/strings/string_view.h"
#include "socket_factory.h"

using std::string;

namespace devtools_goma {

// TLSEngine may not be synchronized.  It must be synchronized externally.
class TLSEngine {
 public:
  // Error type returned by TLS engine.
  enum TLSErrorReason {
    TLS_NO_ERROR = 0,
    TLS_ERROR = -1,
    TLS_WANT_READ = -2,
    TLS_WANT_WRITE = -3,
    TLS_VERIFY_ERROR = -4,
  };

  // Returns true if the transport layer is not ready.
  virtual bool IsIOPending() const = 0;

  // An interface to the transport layer:
  // Sets |data| to be sent to the transport layer.
  // Returns |data| size (>=0) to send or TLSErrorReason if error.
  virtual int GetDataToSendTransport(string* data) = 0;
  // Returns size to be written to the engine.
  virtual size_t GetBufSizeFromTransport() = 0;
  // Sets |data| come from the transport layer.
  // Returns size (>=0) written to the engine or TLSErrorReason if error.
  virtual int SetDataFromTransport(const absl::string_view& data) = 0;

  // An interface to an application:
  // Read and Write return number of read/write bytes if success.
  // Otherwise, TLSErrorReason.
  virtual int Read(void* data, int size) = 0;
  virtual int Write(const void* data, int size) = 0;

  // Returns a human readable last error message.
  virtual string GetLastErrorMessage() const = 0;

  // Returns true if the instance is recycled.
  // This is usually used for skipping initialize process.
  virtual bool IsRecycled() const = 0;

 protected:
  virtual ~TLSEngine() {}
};

// TLSEngineFactory is synchronized.
class TLSEngineFactory : public SocketFactoryObserver {
 public:
  virtual ~TLSEngineFactory() {}
  // Returns new TLSEngine instance used for |sock|.
  // If this get the known |sock|, TLSEngine will be returned from a pool.
  // i.e. caller does not have an ownership of returned value.
  virtual TLSEngine* NewTLSEngine(int sock) = 0;
  // A SocketFactoryObserver interface.
  // Releases TLSEngine associated with the |sock|.
  virtual void WillCloseSocket(int sock) = 0;
  // Returns human readable string of certificates and CRLs TLSEngine's use.
  virtual string GetCertsInfo() = 0;
  // Set a hostname to connect.
  // A subjectAltName of type dNSName in a server certificate should
  // match with |hostname|, or TLSEngine returns TLS_VERIFY_ERROR.
  virtual void SetHostname(const string& hostname) = 0;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TLS_ENGINE_H_
