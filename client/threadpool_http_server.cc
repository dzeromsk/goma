// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// A threadpool HTTP server implementation.

#include "threadpool_http_server.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#else
#include "socket_helper_win.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "absl/strings/str_split.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "fileflag.h"
#include "glog/logging.h"
#include "goma_ipc_addr.h"
#include "goma_ipc_peer.h"
#include "http_util.h"
#include "socket_descriptor.h"
#ifdef _WIN32
#include "named_pipe_server_win.h"
#endif
#include "platform_thread.h"
#include "scoped_fd.h"
#include "simple_timer.h"
#include "trustedipsmanager.h"
#include "util.h"
#include "worker_thread_manager.h"

#define BACKLOG 128

namespace devtools_goma {

// TODO: make it flag?
const int kDefaultTimeoutSec = 900;

ThreadpoolHttpServer::ThreadpoolHttpServer(string listen_addr,
                                           int port,
                                           int num_find_ports,
                                           WorkerThreadManager* wm,
                                           int num_threads,
                                           HttpHandler* http_handler,
                                           int max_num_sockets)
    : listen_addr_(std::move(listen_addr)),
      port_(port),
      port_ready_(false),
      num_find_ports_(num_find_ports),
      wm_(wm),
      pool_(WorkerThreadManager::kFreePool),
      num_http_threads_(num_threads),
      http_handler_(http_handler),
      monitor_(nullptr),
      trustedipsmanager_(nullptr),
      max_num_sockets_(max_num_sockets),
      idle_counting_(true),
      last_closure_id_(kInvalidClosureId) {
  for (int i = 0; i < NUM_SOCKET_TYPES; ++i) {
    max_sockets_[i] = max_num_sockets_;
    num_sockets_[i] = 0;
    idle_counter_[i] = 0;
  }
  if (num_threads > 0) {
    pool_ = wm->StartPool(num_threads, "threadpool_http_server");
    DCHECK_NE(WorkerThreadManager::kFreePool, pool_);
  }
}

ThreadpoolHttpServer::~ThreadpoolHttpServer() {
}

void ThreadpoolHttpServer::SetMonitor(Monitor* monitor) {
  monitor_ = monitor;
}

void ThreadpoolHttpServer::SetTrustedIpsManager(
    TrustedIpsManager* trustedipsmanager) {
  trustedipsmanager_ = trustedipsmanager;
}

#ifdef _WIN32
class ThreadpoolHttpServer::PipeHandler : public NamedPipeServer::Handler {
 public:
  explicit PipeHandler(ThreadpoolHttpServer* server) : server_(server) {}
  ~PipeHandler() override {}

  PipeHandler(const PipeHandler&) = delete;
  PipeHandler& operator=(const PipeHandler&) = delete;

  void HandleIncoming(NamedPipeServer::Request* req) override {
    server_->SendNamedPipeJobToWorkerThread(req);
  }

 private:
  ThreadpoolHttpServer* server_;
};
#endif

void ThreadpoolHttpServer::StartIPC(
    const string& addr, int num_threads,
    int max_overcommit_incoming_sockets) {
#ifdef _WIN32
  pipe_handler_.reset(new PipeHandler(this));
  pipe_server_.reset(new NamedPipeServer(wm_, pipe_handler_.get()));
  pipe_server_->Start(addr);

  // Each thread has a select() for at most FD_SETSIZE of sockets.
  // 1 for event pipe fd.
  // Note that NamedPipeServer doesn't use select(). It only waits for
  // connection for a named pipe, creates new named pipe once
  // the connection is established, and read/write pipes with asynchronous
  // overlapped I/O.
  int max_incoming = std::min(
      max_num_sockets_,
      num_threads * (FD_SETSIZE + max_overcommit_incoming_sockets - 1));
  max_incoming = std::min(
      max_incoming,
      num_http_threads_ * (FD_SETSIZE + max_overcommit_incoming_sockets - 1));
#else
  // compiler_proxy would consume almost 3 fds per request, so it would be
  // safe to limit active accepting sockets by max_num_sockets / 3.
  // Each worker thread has pipe (2 fds) and we use 2 sockets to accept
  // requests, so we count them too.
  int max_incoming = max_num_sockets_ / 3 - num_threads * 2 - 2;
  const int kNumRetry = 10;
  bool socket_ok = false;
  for (int i = 0; i < kNumRetry; ++i) {
    if (OpenUnixDomainSocket(addr)) {
      socket_ok = true;
      break;
    }
    absl::SleepFor(absl::Milliseconds(1));
  }
  CHECK(socket_ok) << "Failed to open " << addr;
  LOG(INFO) << "unix domain:" << addr;

#endif
  LOG(INFO) << "max incoming: " << max_incoming
            << " FD_SETSIZE=" << FD_SETSIZE
            << " max_num_sockets=" << max_num_sockets_
            << " threads=" << num_threads
            << "+" << num_http_threads_;
  CHECK_GT(max_incoming, 0);
  SetAcceptLimit(max_incoming, ThreadpoolHttpServer::SOCKET_IPC);
}

void ThreadpoolHttpServer::StopIPC() {
#ifdef _WIN32
  pipe_server_->Stop();
#else
  CloseUnixDomainSocket();
#endif
}

#ifndef _WIN32
bool ThreadpoolHttpServer::OpenUnixDomainSocket(const string& path) {
  GomaIPCAddr addr;
  socklen_t addr_len = InitializeGomaIPCAddress(path, &addr);
  remove(path.c_str());
  // TODO: use named pipe.
  un_socket_.reset(socket(AF_GOMA_IPC, SOCK_STREAM, 0));
  if (!un_socket_.valid())
    return false;
  CHECK_EQ(0, SetFileDescriptorFlag(un_socket_.get(), FD_CLOEXEC));
  if (!un_socket_.SetNonBlocking()) {
    PLOG(ERROR) << "set non blocking";
    un_socket_.reset(-1);
    return false;
  }
  if (!un_socket_.SetReuseAddr()) {
    PLOG(ERROR) << "setsockopt SO_REUSEADDR";
    un_socket_.reset(-1);
    return false;
  }
  if (bind(un_socket_.get(), (struct sockaddr*)&addr, addr_len) < 0) {
    PLOG(ERROR) << "bind";
    un_socket_.reset(-1);
    return false;
  }
  // drop permission to others.
  if (chmod(path.c_str(), S_IRUSR|S_IWUSR) != 0) {
    PLOG(ERROR) << "chmod";
    un_socket_.reset(-1);
    return false;
  }
  un_socket_name_ = path;
  listen(un_socket_.get(), BACKLOG);
  return true;
}

void ThreadpoolHttpServer::CloseUnixDomainSocket() {
  if (un_socket_.valid()) {
    un_socket_.Close();
    if (!un_socket_name_.empty()) {
      remove(un_socket_name_.c_str());
    }
  }
}
#endif

void ThreadpoolHttpServer::SetAcceptLimit(int n, SocketType socket_type) {
  CHECK_GE(socket_type, 0);
  CHECK_LT(socket_type, NUM_SOCKET_TYPES);
  CHECK_GE(n, 0);
  CHECK_LE(n, max_num_sockets_);

  AUTOLOCK(lock, &mu_);
  max_sockets_[socket_type] = n;
}

/* static */
bool ThreadpoolHttpServer::ParseRequestLine(
    absl::string_view request, string* method,
    string* req_path, string* query) {
  // Find the first request string which would look like
  // 'GET / HTTP/1.1\r\n'
  absl::string_view::size_type pos = request.find("\r\n");
  if (pos == absl::string_view::npos) {
    return false;
  }
  const string firstline = string(request.substr(0, pos));
  std::vector<string> method_path_protocol = ToVector(
      absl::StrSplit(firstline, ' ', absl::SkipEmpty()));
  if (method_path_protocol.size() != 3) {
    return false;
  }
  *method = method_path_protocol[0];
  const string &request_uri(method_path_protocol[1]);
  size_t question_mark;
  if ((question_mark = request_uri.find("?")) != string::npos) {
    *req_path = request_uri.substr(0, question_mark);
    *query =
        request_uri.substr(question_mark + 1,
                           request_uri.size() - question_mark - 1);
  } else {
    *req_path = request_uri;
    query->clear();
  }
  return true;
}

ThreadpoolHttpServer::HttpServerRequest::HttpServerRequest(
    WorkerThreadManager* wm,
    ThreadpoolHttpServer* server,
    const Stat& stat,
    Monitor* monitor)
    : wm_(wm), thread_id_(0),
      server_(server),
      monitor_(monitor),
      request_offset_(0),
      request_content_length_(0),
      request_len_(0),
      parsed_valid_http_request_(false),
      peer_pid_(0),
      stat_(stat) {
}

#ifdef _WIN32
class ThreadpoolHttpServer::RequestFromNamedPipe : public HttpServerRequest {
 public:
  RequestFromNamedPipe(
      WorkerThreadManager* wm,
      ThreadpoolHttpServer* server,
      const Stat& stat,
      Monitor* monitor,
      NamedPipeServer::Request* req)
      : HttpServerRequest(wm, server, stat, monitor),
        req_(req) {
  }
  RequestFromNamedPipe(const RequestFromNamedPipe&) = delete;
  RequestFromNamedPipe& operator=(const RequestFromNamedPipe&) = delete;

  bool IsTrusted() override {
    return CheckCredential();
  }
  bool CheckCredential() override;

  void Start();
  void SendReply(const string& response) override;
  void NotifyWhenClosed(OneshotClosure* callback) override;

 private:
  ~RequestFromNamedPipe() override {}

  NamedPipeServer::Request* req_;
};

bool ThreadpoolHttpServer::RequestFromNamedPipe::CheckCredential() {
  // TODO: get peer_pid_ ?
  return true;
}

void ThreadpoolHttpServer::RequestFromNamedPipe::Start() {
  stat_.waiting_time_msec = stat_.timer.GetInIntMilliseconds();
  stat_.timer.Start();
  thread_id_ = wm_->GetCurrentThreadId();

  request_ = string(req_->request_message());
  request_len_ = request_.size();
  bool request_is_chunked = false;
  if (!FindContentLengthAndBodyOffset(
          request_,
          &request_content_length_,
          &request_offset_,
          &request_is_chunked)) {
    LOG(ERROR) << "failed to find content length and body offset:"
               << request_;
    server_->HandleIncoming(this);
    return;
  }
  // We do not support request encoded with chunked transfer coding.
  if (request_is_chunked) {
    LOG(ERROR) << "request is encoded with chunked transfer coding:"
               << request_;
    server_->HandleIncoming(this);
    return;
  }
  if (request_len_ < request_offset_ + request_content_length_) {
    LOG(ERROR) << "request not fully received? "
               << " len=" << request_len_
               << " offset=" << request_offset_
               << " content_length=" << request_content_length_;
    server_->HandleIncoming(this);
    return;
  }
  stat_.read_req_time_msec = stat_.timer.GetInIntMilliseconds();
  if (!ParseRequestLine(request_,
                        &method_, &req_path_, &query_)) {
    LOG(ERROR) << "parse request line failed";
    server_->HandleIncoming(this);
    return;
  }
  stat_.req_size = request_len_;
  parsed_valid_http_request_ = true;
  server_->HandleIncoming(this);
  return;
}

void ThreadpoolHttpServer::RequestFromNamedPipe::SendReply(
    const string& response) {
  stat_.handler_time_msec = stat_.timer.GetInIntMilliseconds();
  stat_.resp_size = response.size();
  stat_.timer.Start();
  req_->SendReply(response);
  if (monitor_)
    monitor_->FinishHandle(stat_);
  delete this;
}


void ThreadpoolHttpServer::RequestFromNamedPipe::NotifyWhenClosed(
    OneshotClosure* callback) {
  req_->NotifyWhenClosed(callback);
}
#endif

class ThreadpoolHttpServer::RequestFromSocket : public HttpServerRequest {
 public:
  RequestFromSocket(
      WorkerThreadManager* wm,
      ScopedSocket&& sock, SocketType sock_type, const Stat& stat,
      Monitor* monitor,
      TrustedIpsManager* trustedipsmanager,
      ThreadpoolHttpServer* server);
  RequestFromSocket() = delete;
  RequestFromSocket(const RequestFromSocket&) = delete;
  RequestFromSocket& operator=(const RequestFromSocket&) = delete;

  bool CheckCredential() override;
  bool IsTrusted() override;

  void Start();
  void SendReply(const string& response) override;
  void NotifyWhenClosed(OneshotClosure* callback) override;

 private:
  ~RequestFromSocket() override;

  void NotifyWhenClosedInternal(
      WorkerThreadManager::ThreadId thread_id,
      OneshotClosure* callback);
  void DoRead();
  void DoWrite();
  void DoTimeout();
  void ReadFinished();
  void WriteFinished();
  void DoReadEOF();
  void DoCheckClosed();
  void DoClosed();
  void Finish();

  ScopedSocket sock_;
  SocketType socket_type_;
  SocketDescriptor* d_;
  bool request_is_chunked_;
  size_t response_written_;
  TrustedIpsManager* trustedipsmanager_;

  // true if it finished read request, and waiting for ReadFinished()
  // called back.  In other words, callback to ReadFinished on the fly in
  // worker thread manager.
  bool read_finished_;
  // true if it got timed out, and waiting for Finish() called back.
  // In other words, callback to Finish on the fly in worker thread manager.
  bool timed_out_;

  WorkerThreadManager::ThreadId closed_thread_id_;
  OneshotClosure* closed_callback_;
};

ThreadpoolHttpServer::RequestFromSocket::RequestFromSocket(
    WorkerThreadManager* wm,
    ScopedSocket&& sock, SocketType socket_type,
    const ThreadpoolHttpServer::Stat& stat,
    ThreadpoolHttpServer::Monitor* monitor,
    TrustedIpsManager* trustedipsmanager,
    ThreadpoolHttpServer* server)
    : HttpServerRequest(wm, server, stat, monitor),
      sock_(std::move(sock)),
      socket_type_(socket_type),
      d_(nullptr),
      response_written_(0),
      trustedipsmanager_(trustedipsmanager),
      read_finished_(false),
      timed_out_(false),
      closed_thread_id_(0),
      closed_callback_(nullptr) {
}

ThreadpoolHttpServer::RequestFromSocket::~RequestFromSocket() {
  delete closed_callback_;
  ScopedSocket fd(wm_->DeleteSocketDescriptor(d_));
  d_ = nullptr;
  server_->RemoveAccept(socket_type_);
}

bool ThreadpoolHttpServer::RequestFromSocket::CheckCredential() {
  if (socket_type_ != SOCKET_IPC) {
    return false;
  }
  if (d_ == nullptr) {
    return false;
  }
  return CheckGomaIPCPeer(d_->wrapper(), &peer_pid_);
}

bool ThreadpoolHttpServer::RequestFromSocket::IsTrusted() {
  if (trustedipsmanager_ == nullptr)
    return true;
  if (d_ == nullptr) {
    return false;
  }
  union {
    struct sockaddr_storage storage;
    struct sockaddr_in in;
  } addr;
  socklen_t addrlen = sizeof(addr);
  int r = getpeername(d_->fd(), reinterpret_cast<sockaddr*>(&addr), &addrlen);
  if (r != 0) {
    PLOG(WARNING) << "getpeername";
    return false;
  }
  if (addr.storage.ss_family == AF_UNIX) {
    VLOG(1) << "Access from unix domain socket";
    return CheckCredential();
  }
  if (addr.storage.ss_family != AF_INET) {
    LOG(WARNING) << "Access from no-INET:" << addr.storage.ss_family;
    return false;
  }
  bool trusted = trustedipsmanager_->IsTrustedClient(addr.in.sin_addr);
  char buf[128];
  if (trusted) {
    VLOG(1) << "Access from "
            << inet_ntop(AF_INET, &addr.in.sin_addr, buf, sizeof buf)
            << " trusted";
    return true;
  }
  LOG(WARNING) << "Access from "
               << inet_ntop(AF_INET, &addr.in.sin_addr, buf, sizeof buf)
               << " untrusted";
  return false;
}

void ThreadpoolHttpServer::RequestFromSocket::Start() {
  stat_.waiting_time_msec = stat_.timer.GetInIntMilliseconds();
  stat_.timer.Start();
  thread_id_ = wm_->GetCurrentThreadId();
  d_ = wm_->RegisterSocketDescriptor(std::move(sock_),
                                     WorkerThreadManager::PRIORITY_HIGH);

  d_->NotifyWhenReadable(NewPermanentCallback(
      this, &ThreadpoolHttpServer::RequestFromSocket::DoRead));
  d_->NotifyWhenTimedout(
      kDefaultTimeoutSec,
      NewCallback(
          this, &ThreadpoolHttpServer::RequestFromSocket::DoTimeout));
}

void ThreadpoolHttpServer::RequestFromSocket::NotifyWhenClosed(
    OneshotClosure* callback) {
  CHECK(closed_callback_ == nullptr);
  CHECK(callback != nullptr);
  CHECK(read_finished_);
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      NewCallback(
          this,
          &ThreadpoolHttpServer::RequestFromSocket::NotifyWhenClosedInternal,
          wm_->GetCurrentThreadId(),
          callback),
      WorkerThreadManager::PRIORITY_HIGH);
}

void ThreadpoolHttpServer::RequestFromSocket::NotifyWhenClosedInternal(
    WorkerThreadManager::ThreadId thread_id,
    OneshotClosure* callback) {
  CHECK(closed_callback_ == nullptr);
  CHECK(callback != nullptr);
  CHECK(read_finished_);
  closed_thread_id_ = thread_id;
  closed_callback_ = callback;
  d_->NotifyWhenReadable(NewPermanentCallback(
      this, &ThreadpoolHttpServer::RequestFromSocket::DoCheckClosed));
}

void ThreadpoolHttpServer::RequestFromSocket::DoRead() {
  CHECK(d_);
  // If it already got timed out, do nothing.  Eventually, Finish() will be
  // called.
  if (timed_out_)
    return;
  bool found_header = request_offset_ > 0 && request_content_length_ > 0;
  int buf_size = request_.size() - request_len_;
  if (found_header) {
    if (request_.size() < request_offset_ + request_content_length_) {
      request_.resize(request_offset_ + request_content_length_);
    }
  } else if (buf_size < kNetworkBufSize / 2) {
    request_.resize(request_.size() + kNetworkBufSize);
  }
  char* buf = &request_[request_len_];
  buf_size = request_.size() - request_len_;
  CHECK_GT(buf_size, 0)
      << " request_len=" << request_len_
      << " request_.size=" << request_.size()
      << " offset=" << request_offset_
      << " content_length=" << request_content_length_;
  int r = d_->Read(buf, buf_size);
  if (r <= 0) {  // EOF or error
    if (d_->NeedRetry())
      return;
    d_->StopRead();
    read_finished_ = true;
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        NewCallback(
            this, &ThreadpoolHttpServer::RequestFromSocket::ReadFinished),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
    return;
  }
  request_len_ += r;
  absl::string_view req(request_.data(), request_len_);
  if (found_header ||
      FindContentLengthAndBodyOffset(
          req, &request_content_length_, &request_offset_,
          &request_is_chunked_)) {
    // We do not support request encoded with chunked transfer coding.
    if (request_is_chunked_) {  // treat this as error.
      LOG(ERROR) << "request is encoded with chunked transfer coding:"
                 << req;
      d_->StopRead();
      read_finished_ = true;
      wm_->RunClosureInThread(
          FROM_HERE,
          thread_id_,
          NewCallback(
              this, &ThreadpoolHttpServer::RequestFromSocket::ReadFinished),
          WorkerThreadManager::PRIORITY_IMMEDIATE);
      return;
    }
    if (request_len_ < request_offset_ + request_content_length_) {
      // not fully received yet.
      return;
    }
    stat_.read_req_time_msec = stat_.timer.GetInIntMilliseconds();
    if (ParseRequestLine(req, &method_, &req_path_, &query_)) {
      d_->StopRead();
      stat_.req_size = request_len_;
      read_finished_ = true;
      parsed_valid_http_request_ = true;
      wm_->RunClosureInThread(
          FROM_HERE,
          thread_id_,
          NewCallback(
              this, &ThreadpoolHttpServer::RequestFromSocket::ReadFinished),
          WorkerThreadManager::PRIORITY_IMMEDIATE);
    }
  }
}

void ThreadpoolHttpServer::RequestFromSocket::DoWrite() {
  DCHECK(d_);
  int n = d_->Write(
      response_.data() + response_written_,
      response_.size() - response_written_);
  if (n <= 0) {
    if (d_->NeedRetry())
      return;
    d_->StopWrite();
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        NewCallback(
            this, &ThreadpoolHttpServer::RequestFromSocket::Finish),
        WorkerThreadManager::PRIORITY_HIGH);
    return;
  }
  response_written_ += n;
  if (response_written_ == response_.size()) {
    d_->StopWrite();
    stat_.write_resp_time_msec = stat_.timer.GetInIntMilliseconds();
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        NewCallback(
            this, &ThreadpoolHttpServer::RequestFromSocket::WriteFinished),
        WorkerThreadManager::PRIORITY_IMMEDIATE);
  }
}

void ThreadpoolHttpServer::RequestFromSocket::DoTimeout() {
  // If it already finished reading, do nothing.  Eventually, ReadFinished()
  // will be called.
  if (read_finished_)
    return;
  d_->StopRead();
  d_->StopWrite();
  timed_out_ = true;
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      NewCallback(
          this, &ThreadpoolHttpServer::RequestFromSocket::Finish),
      WorkerThreadManager::PRIORITY_HIGH);
}

void ThreadpoolHttpServer::RequestFromSocket::DoCheckClosed() {
  d_->StopRead();
  d_->StopWrite();
  if (!d_->IsReadable() && closed_callback_ != nullptr) {
    VLOG(1) << "closed=" << d_->fd();
  } else {
    PLOG(WARNING) << "readable after request? fd=" << d_->fd();
  }
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      NewCallback(
          this, &ThreadpoolHttpServer::RequestFromSocket::DoClosed),
      WorkerThreadManager::PRIORITY_IMMEDIATE);
}

void ThreadpoolHttpServer::RequestFromSocket::DoClosed() {
  d_->ClearReadable();
  OneshotClosure* callback = closed_callback_;
  closed_callback_ = nullptr;
  if (callback != nullptr) {
    wm_->RunClosureInThread(
        FROM_HERE,
        closed_thread_id_,
        NewCallback(static_cast<Closure*>(callback), &Closure::Run),
        WorkerThreadManager::PRIORITY_HIGH);
  }
}

void ThreadpoolHttpServer::RequestFromSocket::ReadFinished() {
  CHECK(read_finished_);
  stat_.timer.Start();
  d_->ClearReadable();
  d_->ClearTimeout();

  server_->HandleIncoming(this);
}

void ThreadpoolHttpServer::RequestFromSocket::WriteFinished() {
  CHECK(d_);
  d_->ClearWritable();

  d_->ShutdownForSend();
  // Wait for readable, and expecting Read()==0 (EOF).
  d_->NotifyWhenReadable(NewPermanentCallback(
      this, &ThreadpoolHttpServer::RequestFromSocket::DoReadEOF));
}

void ThreadpoolHttpServer::RequestFromSocket::DoReadEOF() {
  CHECK(d_);
  char buf[1];
  int r = d_->Read(buf, sizeof buf);
  if (r == 0) {
    // EOF
    VLOG(1) << d_->fd() << " EOF";
  } else if (r < 0) {
    const string err = d_->GetLastErrorMessage();
    // client may have closed once it had received all response message,
    // before server ack EOF.
    VLOG(1) << "shutdown error? fd=" << d_->fd() << ":" << err;
  } else {
    // unexpected receiving data?
    LOG(WARNING) << "unexpected data after shutdown fd=" << d_->fd();
  }
  d_->StopRead();
  wm_->RunClosureInThread(
      FROM_HERE,
      thread_id_,
      NewCallback(
          this, &ThreadpoolHttpServer::RequestFromSocket::Finish),
      WorkerThreadManager::PRIORITY_HIGH);
}

void ThreadpoolHttpServer::RequestFromSocket::Finish() {
  if (monitor_)
    monitor_->FinishHandle(stat_);
  delete this;
}

void ThreadpoolHttpServer::RequestFromSocket::SendReply(
    const string& response) {
  response_ = response;
  stat_.handler_time_msec = stat_.timer.GetInIntMilliseconds();
  stat_.resp_size = response.size();
  stat_.timer.Start();
  d_->NotifyWhenWritable(
      NewPermanentCallback(
          this, &ThreadpoolHttpServer::RequestFromSocket::DoWrite));
}

void ThreadpoolHttpServer::HandleIncoming(HttpServerRequest* request) {
  if (request->ParsedValidHttpRequest()) {
    http_handler_->HandleHttpRequest(request);
  } else {
    request->SendReply("500 Unexpected Server Error\r\n\r\n");
  }
}

// Returns true if bind succeeded with at most num_find_ports retries.
// The parameter sa and port may be modified when retries happen.
static bool BindPortWithRetries(int fd, int num_find_ports,
                                struct sockaddr_in* sa, int* port) {
  socklen_t sa_size = sizeof(*sa);
  int num_retries = 0;
  int orig_port = *port;
  for (;;) {
    sa->sin_port = htons(static_cast<u_short>(*port));

    if (bind(fd, (struct sockaddr*)sa, sa_size) >= 0) {
      return true;
    }

    if (num_retries < num_find_ports) {
      PLOG(WARNING) << "bind failed for port " << *port
                    << ". We will check the next port...";
      ++num_retries;
      ++*port;
    } else {
      PLOG(ERROR) << "bind failed with " << num_retries << " retries. "
                  << "We checked ports from " << orig_port
                  << " to " << *port << " inclusive.";
      return false;
    }
  }
}

class ThreadpoolHttpServer::IdleClosure {
 public:
  // closure must be a permanent callback.
  IdleClosure(SocketType socket_type,
              int count,
              ThreadpoolHttpServer::RegisteredClosureID id,
              std::unique_ptr<PermanentClosure> closure)
      : socket_type_(socket_type),
        count_(count),
        id_(id),
        closure_(std::move(closure)) {
  }
  ~IdleClosure() {
  }

  SocketType socket_type() const { return socket_type_; }
  int count() const { return count_; }
  ThreadpoolHttpServer::RegisteredClosureID id() const { return id_; }
  PermanentClosure* closure() const { return closure_.get(); }

 private:
  const SocketType socket_type_;
  const int count_;
  const ThreadpoolHttpServer::RegisteredClosureID id_;
  std::unique_ptr<PermanentClosure> closure_;
  DISALLOW_COPY_AND_ASSIGN(IdleClosure);
};

ThreadpoolHttpServer::RegisteredClosureID
ThreadpoolHttpServer::RegisterIdleClosure(
    SocketType socket_type, int count,
    std::unique_ptr<PermanentClosure> closure) {
  DCHECK_GT(count, 0);
  AUTOLOCK(lock, &mu_);
  ++last_closure_id_;
  CHECK_GT(last_closure_id_, kInvalidClosureId);

  idle_closures_.push_back(
      new IdleClosure(socket_type, count,
                      last_closure_id_, std::move(closure)));
  return last_closure_id_;
}

void ThreadpoolHttpServer::UnregisterIdleClosure(
    RegisteredClosureID id) {
  AUTOLOCK(lock, &mu_);
  for (std::vector<IdleClosure*>::iterator iter = idle_closures_.begin();
       iter != idle_closures_.end();
       ++iter) {
    IdleClosure* idle_closure = *iter;
    if (idle_closure->id() == id) {
      delete idle_closure;
      idle_closures_.erase(iter);
      return;
    }
  }

  LOG(ERROR) << "try to unregister invalid closure"
             << " id=" << id;
}

void ThreadpoolHttpServer::UpdateSocketIdleUnlocked(SocketType socket_type) {
  if (!idle_counting_) {
    LOG(INFO) << "update socket type:" << socket_type
              << " while suspending idle counting";
    return;
  }
  if (num_sockets_[socket_type] == 0) {
    ++idle_counter_[socket_type];
    for (size_t i = 0; i < idle_closures_.size(); ++i) {
      IdleClosure* idle_closure = idle_closures_[i];
      if (idle_closure->socket_type() == socket_type &&
          ((idle_counter_[socket_type] % idle_closure->count()) == 0)) {
        LOG(INFO) << "idle closure socket_type:" << socket_type
                  << " idle_counter=" << idle_counter_[socket_type];
        wm_->RunClosure(FROM_HERE,
                        idle_closure->closure(),
                        WorkerThreadManager::PRIORITY_MIN);
      }
    }
  }
}

int ThreadpoolHttpServer::Loop() {
  struct sockaddr_in sa;
  ScopedSocket incoming_socket;  // the main waiting socket
  socklen_t sa_size = sizeof(sa);

  // TODO: listen IPv6 if any.  Need to fix BindPortWithRetries().
  incoming_socket.reset(socket(AF_INET, SOCK_STREAM, 0));
  if (!incoming_socket.valid()) {
    PLOG(ERROR) << "socket";
    return 1;
  }
  CHECK(incoming_socket.SetCloseOnExec());
  CHECK(incoming_socket.SetNonBlocking());

  if (!incoming_socket.SetReuseAddr()) {
    PLOG(ERROR) << "setsockopt SO_REUSEADDR";
    return 1;
  }

  memset(&sa, 0, sizeof(sa));
  if (listen_addr_ == "localhost") {
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (listen_addr_ == "") {
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    LOG(FATAL) << "Unsupported listen_addr:" << listen_addr_;
  }
  sa.sin_family = AF_INET;

  if (!BindPortWithRetries(incoming_socket.get(), num_find_ports_,
                           &sa, &port_)) {
    return 1;
  }

  listen(incoming_socket.get(), BACKLOG);

  if (getsockname(incoming_socket.get(),
                  (struct sockaddr*)&sa, &sa_size) == 0) {
    std::cout << "GOMA version " << kBuiltRevisionString << " is ready."
              << std::endl;
    std::cout << "HTTP server now listening to port " << ntohs(sa.sin_port)
              << ", access with http://localhost:" << ntohs(sa.sin_port)
              << std::endl;
  }
  {
    AUTOLOCK(lock, &mu_);
    port_ready_ = true;
    cond_.Broadcast();
  }
  LOG(INFO) << "listen on port " << ntohs(sa.sin_port);
  for (;;) {
    if (http_handler_->shutting_down()) {
      LOG(INFO) << "Shutting down...";
      un_socket_.reset(-1);
      incoming_socket.reset(-1);
      return 0;
    }
    fd_set read_fd;
    auto max_fd = incoming_socket.get();
    FD_ZERO(&read_fd);
    MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
    FD_SET(incoming_socket.get(), &read_fd);
    MSVC_POP_WARNING();
    if (un_socket_.valid()) {
      MSVC_PUSH_DISABLE_WARNING_FOR_FD_SET();
      FD_SET(un_socket_.get(), &read_fd);
      MSVC_POP_WARNING();
      max_fd = std::max(max_fd, un_socket_.get());
    }
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    int r = select(max_fd + 1, &read_fd, nullptr, nullptr, &tv);
    if (r == 0) {
      // timeout?
      AUTOLOCK(lock, &mu_);
      // 1 sec idle on both socket.
      for (int i = 0; i < NUM_SOCKET_TYPES; ++i) {
        UpdateSocketIdleUnlocked(static_cast<SocketType>(i));
      }
      continue;
    }
    if (r == -1) {
      PLOG(WARNING) << "select";
      continue;
    }
    if (FD_ISSET(incoming_socket.get(), &read_fd)) {
      struct sockaddr_in tmpisa;
      socklen_t addrlen = sizeof(tmpisa);
      ScopedSocket accepted_socket(accept(incoming_socket.get(),
                                          (struct sockaddr*)&tmpisa,
                                          &addrlen));
      if (!accepted_socket.valid()) {
        if (errno == EINTR)
          continue;
        PLOG(ERROR) << "accept incoming_socket";
        return 1;
      }
      AddAccept(SOCKET_TCP);
      if (!accepted_socket.SetCloseOnExec()) {
        LOG(ERROR) << "failed to set FD_CLOEXEC";
        RemoveAccept(SOCKET_TCP);
        accepted_socket.Close();
        return 1;
      }
      // send the new incoming socket to a worker thread.
      SendJobToWorkerThread(std::move(accepted_socket), SOCKET_TCP);
    } else {
      AUTOLOCK(lock, &mu_);
      // tcp was idle, but unix would have some event in 1 sec.
      UpdateSocketIdleUnlocked(SOCKET_TCP);
    }
    if (un_socket_.valid() && FD_ISSET(un_socket_.get(), &read_fd)) {
      GomaIPCAddr tmpaddr;
      socklen_t addrlen = sizeof(tmpaddr);
      ScopedSocket accepted_socket(accept(un_socket_.get(),
                                          (struct sockaddr*)&tmpaddr,
                                          &addrlen));
      if (!accepted_socket.valid()) {
        if (errno == EINTR)
          continue;
        PLOG(ERROR) << "accept unix domain socket";
        if (errno == EMFILE) {
          absl::SleepFor(absl::Seconds(100));
          continue;
        }
        return 1;
      }
      AddAccept(SOCKET_IPC);
      if (!accepted_socket.SetCloseOnExec()) {
        LOG(ERROR) << "failed to set FD_CLOEXEC";
        RemoveAccept(SOCKET_IPC);
        accepted_socket.Close();
        return 1;
      }
      VLOG(1) << "un_socket=" << un_socket_.get()
              << "=>" << accepted_socket;
      SendJobToWorkerThread(std::move(accepted_socket), SOCKET_IPC);
    } else if (un_socket_.valid()) {
      AUTOLOCK(lock, &mu_);
      // unix was idle, but tcp would have some event in 1 sec.
      UpdateSocketIdleUnlocked(SOCKET_IPC);
    }
  }
  // Unreachable
}

void ThreadpoolHttpServer::Wait() {
  AUTOLOCK(lock, &mu_);
  LOG(INFO) << "Wait for http requests...";
  for (;;) {
    bool busy = false;
    for (int i = 0; i < NUM_SOCKET_TYPES; ++i) {
      if (num_sockets_[i] > 0) {
        LOG(INFO) << "socket[" << i << "]=" << num_sockets_[i];
        busy = true;
        break;
      }
    }
    if (busy) {
      cond_.Wait(&mu_);
      continue;
    }
    LOG(INFO) << "All http requests done.";
    return;
  }
}

int ThreadpoolHttpServer::idle_counter(SocketType socket_type) const {
  AUTOLOCK(lock, &mu_);
  return idle_counter_[socket_type];
}

void ThreadpoolHttpServer::SuspendIdleCounter() {
  AUTOLOCK(lock, &mu_);
  LOG(INFO) << "suspend idle counter";
  idle_counting_ = false;
}

void ThreadpoolHttpServer::ResumeIdleCounter() {
  AUTOLOCK(lock, &mu_);
  LOG(INFO) << "resume idle counter";
  idle_counting_ = true;
}

void ThreadpoolHttpServer::AddAccept(SocketType socket_type) {
  AUTOLOCK(lock, &mu_);
  // WorkerThreadManager is using select(2) to handle sockets I/O
  // (for compaibility reason), so it couldn't handle fd >= max_num_sockets_.
  ++num_sockets_[socket_type];
  if (idle_counting_) {
    idle_counter_[socket_type] = 0;
  } else {
    LOG(INFO) << "accept socket type:" << socket_type
              << " while suspending idle counting";
  }
  while ((num_sockets_[socket_type] > max_sockets_[socket_type]) ||
         (num_sockets_[SOCKET_TCP] + num_sockets_[SOCKET_IPC] >=
          max_num_sockets_)) {
    LOG(WARNING) << "Too many accepting socket: "
                 << " tcp:" << num_sockets_[SOCKET_TCP]
                 << " ipc:" << num_sockets_[SOCKET_IPC];
    // Wait some request finishes and release socket by RemoveAccept().
    cond_.Wait(&mu_);
  }
}

void ThreadpoolHttpServer::RemoveAccept(SocketType socket_type) {
  AUTOLOCK(lock, &mu_);
  --num_sockets_[socket_type];
  // Notify some request waiting in AddAccept().
  cond_.Signal();
}

void ThreadpoolHttpServer::WaitPortReady() {
  AUTOLOCK(lock, &mu_);
  while (!port_ready_) {
    LOG(INFO) << "http server is not yet ready";
    cond_.Wait(&mu_);
  }
}

#ifdef _WIN32
void ThreadpoolHttpServer::SendNamedPipeJobToWorkerThread(
    NamedPipeServer::Request* req) {
  WaitPortReady();
  RequestFromNamedPipe* http_server_request =
      new RequestFromNamedPipe(wm_, this, Stat(), monitor_, req);

  wm_->RunClosureInPool(
      FROM_HERE,
      pool_,
      NewCallback(
          http_server_request,
          &ThreadpoolHttpServer::RequestFromNamedPipe::Start),
      WorkerThreadManager::PRIORITY_HIGH);
}
#endif
void ThreadpoolHttpServer::SendJobToWorkerThread(
    ScopedSocket&& socket, SocketType socket_type) {
  WaitPortReady();
  RequestFromSocket* http_server_request =
      new RequestFromSocket(wm_, std::move(socket), socket_type, Stat(),
                            monitor_, trustedipsmanager_, this);
  wm_->RunClosureInPool(
      FROM_HERE,
      pool_,
      NewCallback(
          http_server_request,
          &ThreadpoolHttpServer::RequestFromSocket::Start),
      WorkerThreadManager::PRIORITY_HIGH);
}

}  // namespace devtools_goma
