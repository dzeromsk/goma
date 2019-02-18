// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rpc_controller.h"

#include "glog/logging.h"
#include "prototmp/goma_data.pb.h"
#include "worker_thread_manager.h"

#ifdef _WIN32
#include "callback.h"
#include "compiler_specific.h"
#endif

namespace devtools_goma {

namespace {

// Returns true if header looks like a request coming from browser.
// see also goma_ipc.cc:GomaIPC::SendRequest.
bool IsBrowserRequest(absl::string_view header) {
  // This logic is hard to read. It says:
  // - If `header` contains the string literal, then return false.
  // - If not, then return true.
  // TODO: Use absl::StrContains().
  if (header.find("\r\nHost: 0.0.0.0\r\n") != absl::string_view::npos) {
    return false;
  }
  // TODO: check it doesn't contain Origin header etc?
  return true;
}

}  // namespace

RpcController::RpcController(
    ThreadpoolHttpServer::HttpServerRequest* http_server_request)
    : http_server_request_(http_server_request),
      server_port_(http_server_request->server().port()),
#ifdef _WIN32
      multi_rpc_(nullptr),
#endif
      gomacc_req_size_(0) {
  DCHECK(http_server_request_ != nullptr);
}

RpcController::~RpcController() {
  DCHECK(http_server_request_ == nullptr);
}

#ifdef _WIN32
void RpcController::AttachMultiRpcController(MultiRpcController* multi_rpc) {
  CHECK_EQ(gomacc_req_size_, 0U);
  multi_rpc_ = multi_rpc;
  http_server_request_ = nullptr;
}
#endif

bool RpcController::ParseRequest(ExecReq* req) {
  absl::string_view header = http_server_request_->header();
  if (http_server_request_->request_content_length() <= 0) {
    LOG(WARNING) << "Invalid request from client (no content-length):"
                 << header;
    return false;
  }
  // it won't protect request by using network communications API.
  // https://developer.chrome.com/apps/app_network
  if (IsBrowserRequest(header)) {
    LOG(WARNING) << "Unallowed request from browser:" << header;
    return false;
  }
  if (header.find("\r\nContent-Type: binary/x-protocol-buffer\r\n") ==
      absl::string_view::npos) {
    LOG(WARNING) << "Invalid request from client (invalid content-type):"
                 << header;
    return false;
  }

  gomacc_req_size_ = http_server_request_->request_content_length();
  return req->ParseFromArray(http_server_request_->request_content(),
                             http_server_request_->request_content_length());
}

void RpcController::SendReply(const ExecResp& resp) {
  CHECK(http_server_request_ != nullptr);

  size_t gomacc_resp_size = resp.ByteSize();
  std::ostringstream http_response_message;
  http_response_message << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: binary/x-protocol-buffer\r\n"
                        << "Content-Length: " << gomacc_resp_size << "\r\n\r\n";
  string response_string = http_response_message.str();
  int header_size = response_string.size();
  response_string.resize(header_size + gomacc_resp_size);
  resp.SerializeToArray(&response_string[header_size], gomacc_resp_size);
  http_server_request_->SendReply(response_string);
  http_server_request_ = nullptr;
}

void RpcController::NotifyWhenClosed(OneshotClosure* callback) {
#ifdef _WIN32
  if (multi_rpc_) {
    multi_rpc_->NotifyWhenClosed(callback);
    return;
  }
#endif
  CHECK(http_server_request_ != nullptr);
  http_server_request_->NotifyWhenClosed(callback);
}

#ifdef _WIN32
MultiRpcController::MultiRpcController(
    WorkerThreadManager* wm,
    ThreadpoolHttpServer::HttpServerRequest* http_server_request)
    : wm_(wm),
      caller_thread_id_(wm->GetCurrentThreadId()),
      http_server_request_(http_server_request),
      resp_(new MultiExecResp),
      ALLOW_THIS_IN_INITIALIZER_LIST(closed_callback_(
          NewCallback(this, &MultiRpcController::RequestClosed))),
      gomacc_req_size_(0) {
  DCHECK(http_server_request_ != nullptr);
  http_server_request_->NotifyWhenClosed(closed_callback_);
}

MultiRpcController::~MultiRpcController() {
  DCHECK(http_server_request_ == nullptr);
  CHECK(rpcs_.empty());
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
}

bool MultiRpcController::ParseRequest(MultiExecReq* req) {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  if (http_server_request_->request_content_length() <= 0) {
    LOG(WARNING) << "Invalid request from client (no content-length):"
                 << http_server_request_->request();
    return false;
  }
  gomacc_req_size_ = http_server_request_->request_content_length();
  bool ok = req->ParseFromArray(http_server_request_->request_content(),
                                http_server_request_->request_content_length());
  if (ok) {
    for (int i = 0; i < req->req_size(); ++i) {
      RpcController* rpc = new RpcController(http_server_request_);
      rpc->AttachMultiRpcController(this);
      rpcs_.push_back(rpc);
      resp_->add_response();
    }
    CHECK_EQ(req->req_size(), static_cast<int>(rpcs_.size()));
    CHECK_EQ(req->req_size(), resp_->response_size());
  }
  return ok;
}

RpcController* MultiRpcController::rpc(int i) const {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  DCHECK_GE(i, 0);
  DCHECK_LT(i, static_cast<int>(rpcs_.size()));
  return rpcs_[i];
}

ExecResp* MultiRpcController::mutable_resp(int i) const {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  DCHECK_GE(i, 0);
  DCHECK_LT(i, resp_->response_size());
  return resp_->mutable_response(i)->mutable_resp();
}

bool MultiRpcController::ExecDone(int i) {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  DCHECK_GE(i, 0);
  DCHECK_LT(i, static_cast<int>(rpcs_.size()));
  DCHECK(rpcs_[i] != nullptr);
  delete rpcs_[i];
  rpcs_[i] = nullptr;
  for (const auto* rpc : rpcs_) {
    if (rpc != nullptr)
      return false;
  }
  rpcs_.clear();
  return true;
}

void MultiRpcController::SendReply() {
  CHECK_EQ(caller_thread_id_, wm_->GetCurrentThreadId());
  CHECK(http_server_request_ != nullptr);
  CHECK(rpcs_.empty());

  size_t gomacc_resp_size = resp_->ByteSize();
  std::ostringstream http_response_message;
  http_response_message << "HTTP/1.1 200 OK\r\n"
                        << "Content-Type: binary/x-protocol-buffer\r\n"
                        << "Content-Length: " << gomacc_resp_size << "\r\n\r\n";
  string response_string = http_response_message.str();
  int header_size = response_string.size();
  response_string.resize(header_size + gomacc_resp_size);
  resp_->SerializeToArray(&response_string[header_size], gomacc_resp_size);
  http_server_request_->SendReply(response_string);
  http_server_request_ = nullptr;
}

void MultiRpcController::NotifyWhenClosed(OneshotClosure* callback) {
  // This might be called on the different thread than caller_thread_id_.
  {
    AUTOLOCK(lock, &mu_);
    if (closed_callback_ != nullptr) {
      closed_callbacks_.emplace_back(wm_->GetCurrentThreadId(), callback);
      return;
    }
  }
  // closed_callback_ has been called, that is, http_server_request_
  // was already closed, so runs callback now on the same thread.
  wm_->RunClosureInThread(FROM_HERE, wm_->GetCurrentThreadId(), callback,
                          WorkerThread::PRIORITY_IMMEDIATE);
}

void MultiRpcController::RequestClosed() {
  std::vector<std::pair<WorkerThread::ThreadId, OneshotClosure*>> callbacks;
  {
    AUTOLOCK(lock, &mu_);
    closed_callback_ = nullptr;
    callbacks.swap(closed_callbacks_);
  }
  for (const auto& callback : callbacks) {
    wm_->RunClosureInThread(FROM_HERE, callback.first, callback.second,
                            WorkerThread::PRIORITY_IMMEDIATE);
  }
}
#endif

}  // namespace devtools_goma
