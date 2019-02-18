// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_RPC_CONTROLLER_H_
#define DEVTOOLS_GOMA_CLIENT_RPC_CONTROLLER_H_

#include <memory>
#include <utility>
#include <vector>

#include "basictypes.h"
#include "lockhelper.h"
#include "threadpool_http_server.h"

namespace devtools_goma {

class ExecReq;
class ExecResp;
class OneshotClosure;
class WorkerThreadManager;

#ifdef _WIN32
class MultiExecReq;
class MultiExecResp;
class MultiRpcController;
#endif

class RpcController {
 public:
  explicit RpcController(
      ThreadpoolHttpServer::HttpServerRequest* http_server_request);
  ~RpcController();

#ifdef _WIN32
  // Used as sub-RPC of MultiRpcController.
  // In this case, you can't call ParseRequest/SendReply.
  void AttachMultiRpcController(MultiRpcController* multi_rpc);
#endif
  bool ParseRequest(ExecReq* req);
  void SendReply(const ExecResp& resp);

  // Notifies callback when original request is closed.
  // Can be called from any thread.
  // callback will be called on the thread where this method is called.
  void NotifyWhenClosed(OneshotClosure* callback);

  int server_port() const { return server_port_; }

  size_t gomacc_req_size() const { return gomacc_req_size_; }

 private:
  friend class CompileService;
  ThreadpoolHttpServer::HttpServerRequest* http_server_request_;
  int server_port_;
#ifdef _WIN32
  MultiRpcController* multi_rpc_;
#endif

  size_t gomacc_req_size_;

  DISALLOW_COPY_AND_ASSIGN(RpcController);
};

#ifdef _WIN32
// RpcController for MultiExec.
class MultiRpcController {
 public:
  MultiRpcController(
      WorkerThreadManager* wm,
      ThreadpoolHttpServer::HttpServerRequest* http_server_request);
  ~MultiRpcController();

  // Parses request as MultiExecReq.
  // Also sets up RpcController and ExecResp for each ExecReq
  // in the MultiExecReq.
  bool ParseRequest(MultiExecReq* req);

  RpcController* rpc(int i) const;
  ExecResp* mutable_resp(int i) const;

  // Called when i-th ExecReq in the MultiExecReq has been done,
  // rpc(i) will be invalidated.
  // Returns true if all resp done.
  bool ExecDone(int i);

  void SendReply();

  // Notifies callback when original request is closed.
  // Can be called from any thread.
  // callback will be called on the thread where this method is called.
  void NotifyWhenClosed(OneshotClosure* callback);

 private:
  void RequestClosed();

  WorkerThreadManager* wm_;
  WorkerThread::ThreadId caller_thread_id_;
  ThreadpoolHttpServer::HttpServerRequest* http_server_request_;
  mutable Lock mu_;
  std::vector<RpcController*> rpcs_;
  std::unique_ptr<MultiExecResp> resp_;
  OneshotClosure* closed_callback_;
  std::vector<std::pair<WorkerThread::ThreadId, OneshotClosure*>>
      closed_callbacks_;

  size_t gomacc_req_size_;

  DISALLOW_COPY_AND_ASSIGN(MultiRpcController);
};
#endif

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_RPC_CONTROLLER_H_
