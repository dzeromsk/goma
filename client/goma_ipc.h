// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMA_IPC_H_
#define DEVTOOLS_GOMA_CLIENT_GOMA_IPC_H_

#ifndef _WIN32
#include <arpa/inet.h>
#include <limits.h>
#include <sys/socket.h>
#include <sys/un.h>
#else
#include "socket_helper_win.h"
#endif

#include <memory>
#include <set>
#include <string>

#include "absl/time/time.h"
#include "basictypes.h"

using std::string;

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace devtools_goma {

class Closure;
class IOChannel;
class ScopedSocket;

class GomaIPC {
 public:
  struct Status {
    Status() : initial_timeout(absl::Minutes(3)),
               read_timeout(absl::Seconds(20)),
               check_timeout(absl::Seconds(30)),
               health_check_on_timeout(true),
               connect_success(false), err(0), http_return_code(0),
               req_size(0), resp_size(0) {}

    absl::Duration initial_timeout;
    absl::Duration read_timeout;
    absl::Duration check_timeout;
    bool health_check_on_timeout;

    // Whether connect() was successful for this request.
    bool connect_success;

    // Result of RPC for CallWithAsync. OK=success, or error code.
    int err;
    string error_message;

    // The return code of HTTP.
    int http_return_code;

    // size of (maybe compressed) message.
    size_t req_size;
    size_t resp_size;
    absl::Duration req_send_time;
    absl::Duration resp_recv_time;

    string DebugString() const;
  };

  class ChanFactory {
   public:
    virtual ~ChanFactory() {}

    virtual std::unique_ptr<IOChannel> New() = 0;
    virtual std::string DestName() const = 0;
  };

  // Takes ownership of chan_factory.
  explicit GomaIPC(std::unique_ptr<ChanFactory> chan_factory);
  ~GomaIPC();

  // Returns OK on success, negative (Errno) on failure.
  int Call(const string& path,
           const google::protobuf::Message* req,
           google::protobuf::Message* resp,
           Status* status);

  // Return debug information.
  string DebugString() const;

  // Returns io channel.
  std::unique_ptr<IOChannel> CallAsync(
      const string& path, const google::protobuf::Message* req,
      Status* status);

  // Takes ownership of io channel.
  // Returns OK or Errno.
  int Wait(std::unique_ptr<IOChannel> chan,
           google::protobuf::Message* resp, Status* status);

 private:
  // OK on success, negative (Errno) on failure.
  int SendRequest(const IOChannel* chan,
                  const string& path, const string& s,
                  Status* status);
  // OK on success, negative (Errno) on failure.
  // If read timed-out after status->initial_timeout_sec, it will check /healthz
  // by status->check_timeout_sec intervals if status->health_check_on_timeout
  // is true.
  int ReadResponse(const IOChannel* chan,
                   string* header, string* body, int* http_return_code,
                   Status* status);

  int CheckHealthz(Status* status);

  std::unique_ptr<ChanFactory> chan_factory_;

  DISALLOW_COPY_AND_ASSIGN(GomaIPC);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMA_IPC_H_
