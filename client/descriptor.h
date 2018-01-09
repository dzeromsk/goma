// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_DESCRIPTOR_H_
#define DEVTOOLS_GOMA_CLIENT_DESCRIPTOR_H_

#include <memory>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#else
#include "config_win.h"
#endif

using std::string;

namespace devtools_goma {

class OneshotClosure;
class PermanentClosure;
class SocketDescriptor;

// Descriptor must be used on the same thread where it is created.
// All notification closure will be called on the same thread.
class Descriptor {
 public:
  // closure must be created by NewPermanentCallback.
  // it takes ownership of closure.
  // must not call this in notification closure itself.
  virtual void NotifyWhenReadable(
      std::unique_ptr<PermanentClosure> closure) = 0;
  virtual void NotifyWhenWritable(
      std::unique_ptr<PermanentClosure> closure) = 0;
  virtual void ClearWritable() = 0;
  // closure must be created by NewCallback, that is, one shot closure.
  // must not call this in notification closure itself.
  virtual void NotifyWhenTimedout(double timeout,
                                  OneshotClosure* closure) = 0;
  virtual void ChangeTimeout(double timeout) = 0;

  // Read/Write returns following values:
  //  < 0: I/O error including retriable error.
  //       (A caller should retry Read/Write if NeedRetry is true)
  //  = 0: a connection is closed by a peer.
  //  > 0: number of bytes read/written.
  virtual ssize_t Read(void* ptr, size_t len) = 0;
  virtual ssize_t Write(const void* ptr, size_t len) = 0;
  // NeedRetry is true when previous Read or Write is failed but
  // a caller should retry Read or Write.
  virtual bool NeedRetry() const = 0;
  // CanReuse returns true if underlying socket can be reused.
  virtual bool CanReuse() const = 0;
  virtual string GetLastErrorMessage() const = 0;

  // stop more notification.
  // you can call this in notification closure.
  virtual void StopRead() = 0;
  virtual void StopWrite() = 0;

  virtual SocketDescriptor* socket_descriptor() = 0;

 protected:
  Descriptor() {}
  virtual ~Descriptor() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(Descriptor);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_DESCRIPTOR_H_
