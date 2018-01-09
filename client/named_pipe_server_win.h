// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_SERVER_WIN_H_
#define DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_SERVER_WIN_H_

#ifdef _WIN32

#include <deque>
#include <memory>
#include <set>
#include <string>

#include <AccCtrl.h>
#include <Aclapi.h>

#include "lockhelper.h"
#include "named_pipe_win.h"
#include "string_piece.h"
#include "worker_thread_manager.h"

namespace devtools_goma {

class OneshotClosure;

// NamedPipe server that handle request-response communication like HTTP.
// Each message can't exceed 64KB.
class NamedPipeServer {
 public:
  class Request {
   public:
    virtual ~Request() {}
    virtual StringPiece request_message() const = 0;
    virtual void SendReply(StringPiece reply) = 0;
    virtual void NotifyWhenClosed(OneshotClosure* callback) = 0;
  };
  class Handler {
   public:
    virtual ~Handler() {}
    virtual void HandleIncoming(Request* req) = 0;
  };
  NamedPipeServer(WorkerThreadManager* wm,
                  Handler* handler)
      : wm_(wm),
        thread_id_(0),
        handler_(handler),
        shutting_down_(false) {
  }
  ~NamedPipeServer();

  NamedPipeServer(const NamedPipeServer&) = delete;
  NamedPipeServer(NamedPipeServer&&) = delete;
  NamedPipeServer& operator=(const NamedPipeServer&) = delete;
  NamedPipeServer& operator=(NamedPipeServer&&) = delete;

  void Start(const std::string& name);

  void Stop();

 private:
  class Conn;
  friend class Conn;

  void NotifyWhenClosed(Conn* conn);
  void ReadyToReply(Conn* conn);
  void Run(std::string name);

  bool NewPipe(const std::string& name, OVERLAPPED* overlapped);
  void ReadDone(Conn* conn);
  void ProcessWatchClosed();
  void ProcessReplies();
  void Closed(Conn* conn);
  void WriteDone(Conn* conn);

  void Flusher();
  void ProcessFlushes();

  WorkerThreadManager* wm_;
  WorkerThreadManager::ThreadId thread_id_;  // for Run
  Handler* handler_;

  ScopedNamedPipe pipe_;

  ScopedFd ready_;
  ScopedFd watch_closed_;
  ScopedFd reply_;
  ScopedFd shutdown_;
  ScopedFd done_;
  ScopedFd flush_;
  ScopedFd flusher_done_;

  Lock mu_;
  std::set<Conn*> actives_;
  std::set<Conn*> watches_;
  std::deque<Conn*> replies_;
  std::set<Conn*> finished_;
  std::set<Conn*> flushes_;
  bool shutting_down_;
};

}  // namespace devtools_goma

#endif  // _WIN32

#endif  // DEVTOOLS_GOMA_CLIENT_NAMED_PIPE_SERVER_WIN_H_
