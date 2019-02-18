// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "named_pipe_server_win.h"

#include <deque>
#include <string>

#include "absl/strings/string_view.h"
#include "autolock_timer.h"
#include "callback.h"
#include "config_win.h"
#include "counterz.h"
#include "glog/logging.h"
#include "worker_thread.h"

namespace devtools_goma {

static const int kInputBufSize = 64 * 1024;  // bytes
static const int kOutputBufSize = 128 * 1024;  // bytes
static const int kTimeoutMillisec = 50;

class NamedPipeServer::Conn {
 public:
  Conn(NamedPipeServer* server, ScopedNamedPipe&& pipe)
      : server_(server),
        pipe_(std::move(pipe)),
        thread_id_(server->wm_->GetCurrentThreadId()),
        err_(0),
        written_(0),
        closed_thread_id_(0) {
    memset(&overlapped_, 0, sizeof overlapped_);
    buf_.resize(kInputBufSize);
    req_.reset(new Req(this));
    close_watcher_.reset(new CloseWatcher(this));
  }
  ~Conn() {
    // Cancel all pending I/O before delete of this instance.
    // It is meaningless to proceed pending I/O after the delete,
    // and also cause use-after-free to execute completion routine.
    if (pipe_.get()) {
      if (CancelIo(pipe_.get()) == 0) {
        LOG_SYSRESULT(GetLastError());
        LOG(ERROR) << "cancel io failed: " << this;
      }
    }
  }

  Conn(const Conn&) = delete;
  Conn& operator=(const Conn&) = delete;

  NamedPipeServer::Request* req() const {
    return req_.get();
  }

  bool BelongsToCurrentThread() const {
    return THREAD_ID_IS_SELF(thread_id_);
  }

  bool Start() {
    VLOG(1) << "conn start " << this;
    DCHECK(BelongsToCurrentThread());
    memset(&overlapped_, 0, sizeof overlapped_);
    CHECK_EQ(reinterpret_cast<LPOVERLAPPED>(this), &overlapped_);
    return ReadFileEx(pipe_.get(), &buf_[0], kInputBufSize,
                      &overlapped_,
                      &NamedPipeServer::Conn::ReadCompleted) != 0;
  }

  bool Reply() {
    VLOG(1) << "conn reply " << this;
    DCHECK(BelongsToCurrentThread());

    // stop Read detecting EOF.
    // no need to detect EOF once it starts replying.
    if (CancelIo(pipe_.get()) == 0) {
      LOG_SYSRESULT(GetLastError());
      LOG(ERROR) << "cancel EOF detector " << this;
    }
    {
      AUTOLOCK(lock, &mu_);
      closed_callback_.reset();
    }
    VLOG_IF(1, buf_.size() > kOutputBufSize)
        << "conn reply too large: size=" << buf_.size();
    CHECK_EQ(written_, 0) << "conn reply";
    memset(&overlapped_, 0, sizeof overlapped_);
    CHECK_EQ(reinterpret_cast<LPOVERLAPPED>(this), &overlapped_);
    return WriteFileEx(pipe_.get(), &buf_[written_], buf_.size() - written_,
                       &overlapped_,
                       &NamedPipeServer::Conn::WriteCompleted) != 0;
  }

  void WatchClosed() {
    DCHECK(BelongsToCurrentThread());
    {
      AUTOLOCK(lock, &mu_);
      if (closed_callback_ == nullptr) {
        // WatchClosed might be called after Reply.
        // no need to start close_watcher_.
        return;
      }
    }
    close_watcher_->Run();
  }

  void Flush() {
    if (FlushFileBuffers(pipe_.get()) == 0) {
      LOG_SYSRESULT(GetLastError());
      LOG(ERROR) << "conn failed to flush " << this;
    }
  }

  DWORD error() const { return err_; }

 private:
  class Req : public NamedPipeServer::Request {
   public:
    explicit Req(Conn* conn) : conn_(conn) {}
    ~Req() override {}

    Req(const Req&) = delete;
    Req& operator=(const Req&) = delete;

    absl::string_view request_message() const override {
      return conn_->request_message_;
    }
    void SendReply(absl::string_view reply) override {
      conn_->SendReply(reply);
    }
    void NotifyWhenClosed(OneshotClosure* callback) override {
      conn_->NotifyWhenClosed(callback);
    }

   private:
    Conn* conn_;
  };

  class CloseWatcher {
   public:
    explicit CloseWatcher(Conn* conn) : conn_(conn) {}
    ~CloseWatcher() {}

    CloseWatcher(const CloseWatcher&) = delete;
    CloseWatcher& operator=(const CloseWatcher&) = delete;

    void Run() {
      memset(&overlapped_, 0, sizeof overlapped_);
      // start Read and if it got error, fire close notifier.
      CHECK_EQ(reinterpret_cast<LPOVERLAPPED>(this), &overlapped_);
      if (ReadFileEx(conn_->pipe_.get(), eofBuf_, sizeof eofBuf_,
                     &overlapped_,
                     &NamedPipeServer::Conn::CloseWatcher::EOFDetected) == 0) {
        DWORD err = GetLastError();
        if (err == ERROR_HANDLE_EOF) {
          NotifyClosed(err, 0);
          return;
        }
        LOG_SYSRESULT(err);
        LOG(ERROR) << "conn failed to setup eof detector " << this;
      }
    }

   private:
    static void EOFDetected(
        DWORD err, DWORD num_bytes, LPOVERLAPPED overlapped) {
      VLOG(1) << "EOFDetected err=" << err
              << " num_bytes=" << num_bytes;
      CloseWatcher* cw = reinterpret_cast<CloseWatcher*>(overlapped);
      cw->NotifyClosed(err, num_bytes);
    }

    void NotifyClosed(DWORD err, DWORD num_bytes) {
      if (err == 0) {
        if (GetOverlappedResult(conn_->pipe_.get(), &overlapped_,
                                &num_bytes, FALSE) == 0) {
          LOG_SYSRESULT(GetLastError());
          LOG(ERROR) << "conn close watcher error";
        }
        err = GetLastError();
      }
      conn_->NotifyClosed(err, num_bytes);
    }

    OVERLAPPED overlapped_;
    Conn* conn_;
    char eofBuf_[1];
  };

  void SendReply(absl::string_view reply) {
    buf_.assign(reply.begin(), reply.end());
    server_->ReadyToReply(this);
  }

  void NotifyWhenClosed(OneshotClosure* callback) {
    CHECK(callback != nullptr);
    {
      AUTOLOCK(lock, &mu_);
      CHECK(closed_callback_ == nullptr);
      closed_callback_.reset(callback);
      closed_thread_id_ = server_->wm_->GetCurrentThreadId();
    }
    server_->NotifyWhenClosed(this);
  }

  static void ReadCompleted(
      DWORD err, DWORD num_bytes, LPOVERLAPPED overlapped) {
    VLOG(1) << "ReadCompleted err=" << err
            << " num_bytes=" << num_bytes;
    Conn* conn = reinterpret_cast<Conn*>(overlapped);
    conn->ReadDone(err, num_bytes);
  }

  static void WriteCompleted(
      DWORD err, DWORD num_bytes, LPOVERLAPPED overlapped) {
    VLOG(1) << "WriteCompleted err=" << err
            << " num_bytes=" << num_bytes;
    Conn* conn = reinterpret_cast<Conn*>(overlapped);
    conn->WriteDone(err, num_bytes);
  }

  void ReadDone(DWORD err, DWORD num_bytes) {
    DCHECK(BelongsToCurrentThread());
    err_ = err;
    // num_bytes = 0 means some error happens.
    if (num_bytes > 0) {
      request_message_ = absl::string_view(buf_.data(), num_bytes);
    } else {
      request_message_ = absl::string_view();
    }
    server_->ReadDone(this);
  }

  void NotifyClosed(DWORD err, DWORD num_bytes) {
    DCHECK(BelongsToCurrentThread());
    if (err == ERROR_OPERATION_ABORTED) {
      // I/O operation were canceled.  No need to notify.
      return;
    }
    LOG(INFO) << "named pipe closed. err=" << err;
    err_ = err;
    server_->Closed(this);
    OneshotClosure* callback = nullptr;
    WorkerThread::ThreadId thread_id;
    {
      AUTOLOCK(lock, &mu_);
      callback = closed_callback_.release();
      thread_id = closed_thread_id_;
    }
    if (callback != nullptr) {
      CHECK_NE(thread_id, 0U);
      server_->wm_->RunClosureInThread(
          FROM_HERE,
          thread_id,
          NewCallback(static_cast<Closure*>(callback), &Closure::Run),
          WorkerThread::PRIORITY_HIGH);
    }
  }

  void WriteDone(DWORD err, DWORD num_bytes) {
    DCHECK(BelongsToCurrentThread());
    err_ = err;
    if (err == 0) {
      BOOL r = false;
      if (GetOverlappedResult(pipe_.get(), &overlapped_,
                              &num_bytes, FALSE)) {
        if (num_bytes > 0) {
          written_ += num_bytes;
          if (written_ == buf_.size()) {
            server_->WriteDone(this);
            return;
          }
          CHECK_LT(written_, buf_.size()) << "conn write overrun?";
          memset(&overlapped_, 0, sizeof overlapped_);
          CHECK_EQ(reinterpret_cast<LPOVERLAPPED>(this), &overlapped_);
          r = WriteFileEx(pipe_.get(),
                          &buf_[written_], buf_.size() - written_,
                          &overlapped_,
                          &NamedPipeServer::Conn::WriteCompleted);
          if (r != 0) {
            return;
          }
        }
        LOG(ERROR) << "conn write num_bytes=" << num_bytes
                   << " written=" << written_
                   << " WriteFileEx=" << r;
      }
      err = GetLastError();
      if (err == ERROR_IO_PENDING) {
        // never happens?
        return;
      }
      err_ = err;
    }
    LOG_SYSRESULT(err);
    LOG(ERROR) << "conn write done error err=" << err
               << " num_bytes=" << num_bytes
               << " buf_size=" << buf_.size()
               << " written=" << written_;
    server_->WriteDone(this);
  }

  OVERLAPPED overlapped_;  // should be initial member at offset 0.
  NamedPipeServer* server_;
  ScopedNamedPipe pipe_;
  WorkerThread::ThreadId thread_id_;
  DWORD err_;
  std::vector<char> buf_;
  absl::string_view request_message_;
  size_t written_;

  mutable Lock mu_;
  WorkerThread::ThreadId closed_thread_id_ GUARDED_BY(mu_);
  std::unique_ptr<OneshotClosure> closed_callback_ GUARDED_BY(mu_);

  std::unique_ptr<Req> req_;
  std::unique_ptr<CloseWatcher> close_watcher_;
};

NamedPipeServer::~NamedPipeServer() {
  CHECK(!ready_.valid());
  CHECK(!watch_closed_.valid());
  CHECK(!reply_.valid());
  CHECK(!shutdown_.valid());
  CHECK(!done_.valid());
  CHECK(!flush_.valid());
  CHECK(!flusher_done_.valid());
  CHECK(actives_.empty());
  CHECK(replies_.empty());
  CHECK(finished_.empty());
  CHECK(flushes_.empty());
}

void NamedPipeServer::Start(const std::string& name) {
  LOG(INFO) << "Start for " << name;
  ready_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
  if (!ready_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for ready";
  }
  watch_closed_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
  if (!watch_closed_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for watch_closed";
  }
  reply_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
  if (!reply_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for reply";
  }
  shutdown_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
  if (!shutdown_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for shutdown";
  }
  done_.reset(CreateEvent(nullptr, TRUE, FALSE, nullptr));
  if (!done_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for done";
  }

  flush_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
  if (!flush_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for flush";
  }

  flusher_done_.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
  if (!flusher_done_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for flusher";
  }
  wm_->NewThread(NewCallback(this, &NamedPipeServer::Flusher),
                 "pipe_flusher");

  wm_->NewThread(NewCallback(this, &NamedPipeServer::Run, name),
                 "pipe_server");

  DWORD w = WaitForSingleObject(ready_.handle(), 10*1000); // 10 secs timeout
  if (w != WAIT_OBJECT_0) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to wait for ready: w=" << w;
  }
}

void NamedPipeServer::Stop() {
  LOG(INFO) << "Stop";
  if (!shutdown_.valid() || !done_.valid()) {
    LOG(INFO) << "not running?";
    return;
  }
  {
    AUTOLOCK(lock, &mu_);
    shutting_down_ = true;
  }
  if (!SetEvent(shutdown_.handle())) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to signal shutdown";
  }
  bool finished = false;
  HANDLE events[2];
  events[0] = done_.handle();
  events[1] = flusher_done_.handle();
  while (!finished) {
    DWORD w = WaitForMultipleObjectsEx(
        2, events,
        TRUE,  // wait all,
        INFINITE,
        TRUE);
    switch (w) {
      case WAIT_OBJECT_0:
        ABSL_FALLTHROUGH_INTENDED;
      case WAIT_OBJECT_0 + 1:
        finished = true;
        break;
      case WAIT_IO_COMPLETION:
        continue;
      default:
        LOG_SYSRESULT(GetLastError());
        LOG(FATAL) << "Failed to wait for done: w=" << w;
    }
  }
  LOG(INFO) << "done";
  ready_.Close();
  watch_closed_.Close();
  reply_.Close();
  shutdown_.Close();
  done_.Close();
  flush_.Close();
  flusher_done_.Close();

  std::unordered_set<Conn*> conns;
  {
    AUTOLOCK(lock, &mu_);
    conns.insert(actives_.begin(), actives_.end());
    actives_.clear();
    conns.insert(replies_.begin(), replies_.end());
    replies_.clear();
    conns.insert(finished_.begin(), finished_.end());
    finished_.clear();
    conns.insert(flushes_.begin(), flushes_.end());
    flushes_.clear();
  }
  for (const auto* conn : conns) {
    delete conn;
  }
}

void NamedPipeServer::ReadyToReply(Conn* conn) {
  {
    AUTOLOCK(lock, &mu_);
    actives_.erase(conn);
    watches_.erase(conn);
    if (shutting_down_) {
      LOG(WARNING) << "will not update replies_ because shutting down.";
      delete conn;
      return;
    } else {
      replies_.push_back(conn);
    }
  }
  if (!SetEvent(reply_.handle())) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to signal reply";
  }
}

void NamedPipeServer::NotifyWhenClosed(Conn* conn) {
  {
    AUTOLOCK(lock, &mu_);
    watches_.insert(conn);
  }
  if (!SetEvent(watch_closed_.handle())) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to signal watch_closed";
  }
}

void NamedPipeServer::Run(std::string name) {
  thread_id_ = wm_->GetCurrentThreadId();
  std::string pipename = "\\\\.\\pipe\\" + name;
  LOG(INFO) << "Run pipe=" << pipename;

  ScopedFd connected(CreateEvent(
      nullptr,  // default security attribute
      TRUE,  // manual reset event
      TRUE,  // initial state = signaled
      nullptr));
  if (!connected.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to create event for connect";
  }

  OVERLAPPED o_connect;
  o_connect.hEvent = connected.handle();
  bool is_pending = NewPipe(pipename, &o_connect);

  if (!SetEvent(ready_.handle())) {
    LOG_SYSRESULT(GetLastError());
    LOG(FATAL) << "Failed to signal ready";
  }
  LOG(INFO) << "pipe=" << pipename << " ready";
  HANDLE events[4];
  events[0] = connected.handle();
  events[1] = watch_closed_.handle();
  events[2] = reply_.handle();
  events[3] = shutdown_.handle();
  for (;;) {
    DWORD w = WaitForMultipleObjectsEx(
        4, events,
        FALSE,  // wait all
        INFINITE,
        TRUE);
    GOMA_COUNTERZ("After WaitForMultipleObjectsEx");
    switch (w) {
      case WAIT_OBJECT_0:  // connected
        if (is_pending) {
          DWORD num_bytes = 0;
          BOOL ok = GetOverlappedResult(
              pipe_.get(),
              &o_connect,
              &num_bytes,
              FALSE);
          if (!ok) {
            LOG_SYSRESULT(GetLastError());
            LOG(ERROR) << "Failed to GetOverlappedResult for connect";
            return;
          }
        }
        if (pipe_.valid()) {
          GOMA_COUNTERZ("new Conn and etc.");
          VLOG(1) << "connected";
          Conn* conn = new Conn(this, std::move(pipe_));
          {
            AUTOLOCK(lock, &mu_);
            actives_.insert(conn);
          }
          if (!conn->Start()) {
            LOG(ERROR) << "conn start failed";
            {
              AUTOLOCK(lock, &mu_);
              actives_.erase(conn);
            }
            delete conn;
          }
        }
        is_pending = NewPipe(pipename, &o_connect);
        VLOG(1) << "new pipe is_pending=" << is_pending;
        break;

      case WAIT_OBJECT_0 + 1:  // watch closed
        VLOG(1) << "watch closed";
        ProcessWatchClosed();
        break;

      case WAIT_OBJECT_0 + 2:  // ready to reply
        VLOG(1) << "ready to reply";
        ProcessReplies();
        break;

      case WAIT_OBJECT_0 + 3:
        LOG(INFO) << "shutting down";
        if (CancelIo(pipe_.get()) == 0) {
          LOG_SYSRESULT(GetLastError());
          LOG(ERROR) << "cancel connect named pipe";
        }
        if (!SetEvent(done_.handle())) {
          LOG_SYSRESULT(GetLastError());
          LOG(FATAL) << "Failed to signal done";
        }
        return;

      case WAIT_IO_COMPLETION:
        VLOG(2) << "io completion";
        // The wait is satisfied by a completed read or write operation.
        // This allows the system to execute the completion routine.
        break;

      default:
        LOG_SYSRESULT(GetLastError());
        LOG(FATAL) << "WaitForMultipleObjectsEx";
        return;
    }
  }
}

bool NamedPipeServer::NewPipe(
    const std::string& pipename, OVERLAPPED* overlapped) {
  GOMA_COUNTERZ("");
  DCHECK(THREAD_ID_IS_SELF(thread_id_));

  pipe_ = ScopedNamedPipe(
      CreateNamedPipeA(pipename.c_str(),
                       PIPE_ACCESS_DUPLEX |
                         FILE_FLAG_OVERLAPPED,
                       PIPE_TYPE_BYTE |
                         PIPE_READMODE_BYTE |
                         PIPE_WAIT |
                         PIPE_REJECT_REMOTE_CLIENTS,
                       PIPE_UNLIMITED_INSTANCES,
                       kOutputBufSize,
                       kInputBufSize,
                       kTimeoutMillisec,
                       nullptr));  // TODO: set security attributes.
  if (!pipe_.valid()) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to open pipe " << pipename;
    return false;
  }

  // TODO: Make calling ConnectNamedpipe loop as quickly as possible or
  //               make the loop multi-threaded.
  if (ConnectNamedPipe(pipe_.get(), overlapped)) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to ConnectNamedPipe";
    return false;
  }
  switch (GetLastError()) {
    case ERROR_IO_PENDING:
      // overlapped connection in progress.
      return true;

    case ERROR_PIPE_CONNECTED:
      // client is already connected, signal.
      if (SetEvent(overlapped->hEvent)) {
        break;
      }
      ABSL_FALLTHROUGH_INTENDED;
    default:
      LOG_SYSRESULT(GetLastError());
      LOG(ERROR) << "Failed to ConnectNamedPipe";
  }
  return false;
}

void NamedPipeServer::ReadDone(Conn* conn) {
  VLOG(1) << "ReadDone err=" << conn->error();
  DCHECK(THREAD_ID_IS_SELF(thread_id_));
  if (conn->error() != 0) {
    LOG(ERROR) << "Read error:" << conn->error();
    {
      AUTOLOCK(lock, &mu_);
      actives_.erase(conn);
    }
    delete conn;
    return;
  }
  wm_->RunClosure(FROM_HERE,
                  NewCallback(handler_,
                              &NamedPipeServer::Handler::HandleIncoming,
                              conn->req()),
                  WorkerThread::PRIORITY_HIGH);
}

void NamedPipeServer::ProcessWatchClosed() {
  VLOG(1) << "ProcessWatchClosed";
  DCHECK(THREAD_ID_IS_SELF(thread_id_));
  std::unordered_set<Conn*> watches;
  {
    AUTOLOCK(lock, &mu_);
    watches.swap(watches_);
  }
  for (auto& conn : watches) {
    VLOG(1) << "process watch conn=" << conn;
    conn->WatchClosed();
  }
}

void NamedPipeServer::ProcessReplies() {
  VLOG(1) << "ProcessReplies";
  DCHECK(THREAD_ID_IS_SELF(thread_id_));
  std::deque<Conn*> replies;
  {
    AUTOLOCK(lock, &mu_);
    replies.swap(replies_);
  }
  for (auto& conn : replies) {
    VLOG(1) << "process reply conn=" << conn;
    if (!conn->Reply()) {
      LOG_SYSRESULT(GetLastError());
      LOG(WARNING) << "Reply error";
      {
        AUTOLOCK(lock, &mu_);
        CHECK_EQ(watches_.count(conn), 0U);
      }
      delete conn;
    } else {
      AUTOLOCK(lock, &mu_);
      finished_.insert(conn);
    }
  }
}

void NamedPipeServer::Closed(Conn* conn) {
  DCHECK(THREAD_ID_IS_SELF(thread_id_));
  VLOG(1) << "Closed";
  AUTOLOCK(lock, &mu_);
  actives_.erase(conn);
}

void NamedPipeServer::WriteDone(Conn* conn) {
  DCHECK(THREAD_ID_IS_SELF(thread_id_));
  VLOG(1) << "WriteDone";
  {
    AUTOLOCK(lock, &mu_);
    CHECK_EQ(watches_.count(conn), 0U);
    finished_.erase(conn);
    flushes_.insert(conn);
  }
  if (!SetEvent(flush_.handle())) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Failed to signal flush";
  }
}

void NamedPipeServer::Flusher() {
  LOG(INFO) << "Run flusher";

  HANDLE events[2];
  events[0] = flush_.handle();
  events[1] = shutdown_.handle();
  for (;;) {
    DWORD w = WaitForMultipleObjectsEx(
        2, events,
        FALSE,  // wait all
        INFINITE,
        TRUE);
    GOMA_COUNTERZ("After WaitForMultipleObjectsEx");
    switch (w) {
      case WAIT_OBJECT_0:  // flush
        ProcessFlushes();
        break;

      case WAIT_OBJECT_0 + 1:  // shutdown
        LOG(INFO) << "shutting down";
        if (!SetEvent(flusher_done_.handle())) {
          LOG_SYSRESULT(GetLastError());
          LOG(FATAL) << "Failed to signal done";
        }
        return;
      case WAIT_IO_COMPLETION:
        break;
      default:
        LOG_SYSRESULT(GetLastError());
        LOG(FATAL) << "WaitForMultipleObjectsEx";
        return;
    }
  }
}

void NamedPipeServer::ProcessFlushes() {
  GOMA_COUNTERZ("");

  VLOG(1) << "ProcessFlushes";
  std::unordered_set<Conn*> flushes;
  {
    AUTOLOCK(lock, &mu_);
    flushes.swap(flushes_);
  }
  for (auto& conn : flushes) {
    VLOG(1) << "process flush conn=" << conn;
    conn->Flush();
    {
      AUTOLOCK(lock, &mu_);
      CHECK_EQ(watches_.count(conn), 0U);
      CHECK_EQ(finished_.count(conn), 0U);
    }
    delete conn;
  }
}

}  // namespace devtools_goma
