// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_SUBPROCESS_CONTROLLER_H_
#define DEVTOOLS_GOMA_CLIENT_SUBPROCESS_CONTROLLER_H_

#include <memory>
#include <set>
#include <string>

#include "basictypes.h"
#include "scoped_fd.h"

#ifdef _WIN32
#include "socket_helper_win.h"
#endif

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace devtools_goma {

class ScopedSocket;
class SubProcessKill;
class SubProcessReq;
class SubProcessRun;
class SubProcessSetOption;
class SubProcessStarted;
class SubProcessTerminated;

// SubProcessController consists of server and client.
// A SubProcessController server runs in single threaded process and manages
// SubProcessImpl and actual subprocesses.
// A SubProcessController client runs with worker thread manager and serves
// for SubProcessTask.
// These server and client communicate with a socket created by socketpair(2).

// message format:
//   op: first int
//   len: second int
//   payload: len bytes
//    client->server
//      op=REGISTER;    payload: serialized SubProcessReq
//      op=REQUEST_RUN; payload: serialized SubProcessRun
//      op=KILL;        payload: serialized SubProcessKill
//      op=SET_OPTION;  payload: serialized SubProcessSetOption
//    server->client
//      op=STARTED;     payload: serialized SubProcessStarted
//      op=TERMINATED;  payload: serialized SubProcessTerminated
class SubProcessController {
 public:
  struct Options {
    Options();
    int max_subprocs;
    int max_subprocs_low_priority;
    int max_subprocs_heavy_weight;
    bool dont_kill_subprocess;

    std::string DebugString() const;
  };
  enum Op {
    CLOSED = -1,
    NOP = 0,

    // request: client -> server
    REGISTER = 1,
    REQUEST_RUN = 2,
    KILL = 3,
    SET_OPTION = 4,
    SHUTDOWN = 5,

    // response: server -> client
    STARTED = 10,
    TERMINATED = 11,
  };

  // Initializes SubProcessController subsystem.
  // Must be called before creating threads.
  static void Initialize(const char* arg0, const Options& options);
  virtual ~SubProcessController();

  // Register subproc.  Takes ownership of req.
  // Client -> server
  virtual void Register(std::unique_ptr<SubProcessReq> req) = 0;

  // Request to run the subproc.  Takes ownership of run.
  // Client -> server
  virtual void RequestRun(std::unique_ptr<SubProcessRun> run) = 0;

  // Kills the subproc.  Takes ownership of kill.
  // Client -> server
  virtual void Kill(std::unique_ptr<SubProcessKill> kill) = 0;

  // Sets option. Takes the ownership of |option|.
  // Client -> server.
  virtual void SetOption(std::unique_ptr<SubProcessSetOption> option) = 0;

  // Notifies the subproc is started.  Takes ownership of started.
  // This function takes raw pointer because it is used for
  // NewCallback in SubProcessControllerClient::DoRead().
  // Server -> client.
  virtual void Started(std::unique_ptr<SubProcessStarted> started) = 0;

  // Notifies the subproc is terminated.  Takes ownership of terminated.
  // This function takes raw pointer because it is used for
  // NewCallback in SubProcessControllerClient::DoRead().
  // Server -> client.
  virtual void Terminated(std::unique_ptr<SubProcessTerminated> terminated) = 0;

 protected:
  SubProcessController();

  // Adds message of op, with message to pending_write_.
  // Returns true if it is the initial request in pending_write_.
  // Must use on the same thread everytime.
  bool AddMessage(int op, const google::protobuf::Message& message);

  bool has_pending_write() const;

  // Writes pending_write_ message through fd.
  // Returns true if no data to write, or data successfuly written.
  // Returns false if I/O error happens.
  bool WriteMessage(const IOChannel* fd);

  // Reads message through fd.
  // If it returns true, you can read payload data in payload_data().
  // Once you processed data, you need to call ReadDone().
  // Must use on the same thread everytime.
  bool ReadMessage(const IOChannel* fd, int* op, int* len);

  // Access payload data read by ReadMessage.
  // Valid only between ReadMessage() and ReadDone().
  const char* payload_data() const;

  // Discards read message.
  void ReadDone();

#ifdef _WIN32
  static unsigned __stdcall StartServer(void* thread_params);
#else
  [[noreturn]] static void OnPeerShutdowned(bool shutdowned);
#endif

  static const size_t kMessageHeaderLen;
  static const size_t kOpOffset;
  static const size_t kSizeOffset;

 private:
  std::string pending_write_;
  std::string pending_read_;
  size_t read_len_;
  DISALLOW_COPY_AND_ASSIGN(SubProcessController);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SUBPROCESS_CONTROLLER_H_
