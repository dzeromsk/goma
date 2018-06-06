// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "http_rpc.h"

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_specific.h"
#include "glog/logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "google/protobuf/message.h"
#include "google/protobuf/io/gzip_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
MSVC_POP_WARNING()
#include "ioutil.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "scoped_fd.h"
#include "simple_timer.h"
#include "worker_thread_manager.h"

using std::string;

namespace devtools_goma {

class HttpRPC::Request : public HttpClient::Request {
 public:
  Request(const google::protobuf::Message* req,
          HttpRPC::Status* status) :
      req_(req),
      status_(status) {
  }
  ~Request() override {}

  string CreateMessage() const override = 0;

  std::unique_ptr<HttpClient::Request> Clone() const override = 0;

 protected:
  const google::protobuf::Message* req_;
  HttpRPC::Status* status_;

 private:
  DISALLOW_ASSIGN(Request);
};

class HttpRPC::CallRequest : public HttpRPC::Request {
 public:
  CallRequest(const google::protobuf::Message* req, HttpRPC::Status* status);
  ~CallRequest() override {}
  void EnableCompression(int level, const string& accept_encoding) {
    compression_level_ = level;
    accept_encoding_ = accept_encoding;
  }
  string CreateMessage() const override;

  std::unique_ptr<HttpClient::Request> Clone() const override {
    return std::unique_ptr<HttpClient::Request>(
        new HttpRPC::CallRequest(*this));
  }

 private:
  int compression_level_;
  string accept_encoding_;
  DISALLOW_ASSIGN(CallRequest);
};

class HttpRPC::Response : public HttpClient::Response {
 public:
  Response(google::protobuf::Message* resp,
           HttpRPC::Status* status)
      : resp_(resp),
        status_(status) {
  }
  ~Response() override {}

 protected:
  void ParseBody(google::protobuf::io::ZeroCopyInputStream* input) override = 0;

  google::protobuf::Message* resp_;
  HttpRPC::Status* status_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Response);
};

class HttpRPC::CallResponse : public HttpRPC::Response {
 public:
  CallResponse(google::protobuf::Message* resp,
               HttpRPC::Status* status)
      : Response(resp, status) {}
  ~CallResponse() override {}
  void ParseBody(google::protobuf::io::ZeroCopyInputStream* input) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(CallResponse);
};

class HttpRPC::CallData {
 public:
  CallData(std::unique_ptr<HttpRPC::Request> req,
           std::unique_ptr<HttpRPC::Response> resp,
           OneshotClosure* callback)
      : req_(std::move(req)),
        resp_(std::move(resp)),
        callback_(callback) {
  }
  ~CallData() {
    if (callback_) {
      callback_->Run();
    }
  }

  HttpRPC::Request* req() const { return req_.get(); }
  HttpRPC::Response* resp() const { return resp_.get(); }

 private:
  std::unique_ptr<HttpRPC::Request> req_;
  std::unique_ptr<HttpRPC::Response> resp_;
  OneshotClosure* callback_;
  DISALLOW_COPY_AND_ASSIGN(CallData);
};

HttpRPC::Options::Options()
    : compression_level(0),
      start_compression(false) {
}

string HttpRPC::Options::DebugString() const {
  std::ostringstream ss;
  ss << " compression_level=" << compression_level;
  if (start_compression)
    ss << " start_compression";
  ss << " accept_encoding=" << accept_encoding;
  ss << " content_type_for_protobuf=" << content_type_for_protobuf;
  return ss.str();
}

HttpRPC::HttpRPC(HttpClient* client,
                 const Options& options)
    : client_(client),
      options_(options),
      compression_enabled_(options.start_compression) {
  LOG(INFO) << options_.DebugString();
  CHECK(!options_.content_type_for_protobuf.empty());
  CHECK(options_.content_type_for_protobuf.find_first_of("\r\n")
        == string::npos)
        << "content_type_for_protobuf must not contain CR LF:"
        << options_.content_type_for_protobuf;
}

HttpRPC::~HttpRPC() {
  LOG(INFO) << "HttpRPC terminated.";
}

int HttpRPC::Ping(WorkerThreadManager* wm,
                  const string& path,
                  Status* status) {
  std::unique_ptr<Status> ping_status(new Status);
  DCHECK(status);
  *ping_status = *status;
  if (ping_status->trace_id.empty()) {
    ping_status->trace_id = "ping";
  }
  long long timeout_secs = -1;
  if (!ping_status->timeout_secs.empty()) {
    timeout_secs = ping_status->timeout_secs.front();
    LOG(INFO) << "ping " << path << " timeout=" << timeout_secs;
  } else {
    LOG(INFO) << "ping " << path << " no timeout";
  }
  DCHECK(wm) << "There isn't any worker thread to send to";
  // Make client active until PingDone is called.
  // Without this, client could shutdown after ping rpc is finished
  // and before it calls Wait in PingDone.
  client_->IncNumActive();
  std::unique_ptr<SimpleTimer> timer(new SimpleTimer);
  // Ping may be called on the thread not in the worker thread manager.
  wm->RunClosure(
      FROM_HERE,
      NewCallback(
          this, &HttpRPC::DoPing, path, ping_status.get()),
      WorkerThreadManager::PRIORITY_LOW);
  // We can't use Wait() since wm->Dispatch() can be called
  // on a thread in the worker thread manager only.
  // TODO: use conditional variable to wait?
  while (!ping_status->finished) {
    PlatformThread::Sleep(100);
    if (timeout_secs > 0 &&
        timer->GetInNanoSeconds() > timeout_secs * 1000000000) {
      // TODO: fix HttpRPC's timeout.
      LOG(ERROR) << "ping timed out, but not finished yet."
                 << "timer=" << timer->GetInMilliSeconds() << " [ms]";
      break;
    }
  }
  *status = *ping_status;
  wm->RunClosure(
      FROM_HERE,
      NewCallback(this, &HttpRPC::PingDone,
                  std::move(ping_status), std::move(timer)),
      WorkerThreadManager::PRIORITY_LOW);
  int status_code = client_->UpdateHealthStatusMessageForPing(
      static_cast<const HttpClient::Status&>(*status), -1);
  const string& health_status = client_->GetHealthStatusMessage();
  if (health_status != "ok") {
    LOG(WARNING) << "Update health status:" << health_status;
  }
  return status_code;
}

void HttpRPC::DoPing(string path, Status* status) {
  CallWithCallback(path, nullptr, nullptr, status, nullptr);
}

void HttpRPC::PingDone(std::unique_ptr<Status> status,
                       std::unique_ptr<SimpleTimer> timer) {
  LOG(INFO) << "Wait ping status " << status.get();
  Wait(status.get());
  int round_trip_time = timer->GetInMs();
  LOG_IF(WARNING, !status->connect_success)
      << "failed to connect to backend servers";
  LOG_IF(WARNING, status->err == ERR_TIMEOUT)
      << "timed out to send request to backend servers";
  LOG_IF(WARNING, status->http_return_code != 200)
      << "http=" << status->http_return_code;
  LOG_IF(WARNING, !status->err_message.empty())
      << "http err_message=" << status->err_message;
  LOG_IF(WARNING, !status->response_header.empty())
      << "http response header=" << status->response_header;
  LOG_IF(WARNING, status->err != OK)
      << "http status err=" << status->err;
  const string old_health_status = client_->GetHealthStatusMessage();
  client_->UpdateHealthStatusMessageForPing(
      static_cast<const HttpClient::Status&>(*status), round_trip_time);
  const string new_health_status = client_->GetHealthStatusMessage();
  if (old_health_status != new_health_status) {
    if (new_health_status == "ok") {
      LOG(INFO) << "Update health status:" << old_health_status
                << " to " << new_health_status;
    } else {
      LOG(WARNING) << "Update health status:" << old_health_status
                   << " to " << new_health_status;
    }
  }
  LOG(INFO) << "Release ping status " << status.get();
  client_->DecNumActive();
}

int HttpRPC::Call(const string& path,
                  const google::protobuf::Message* req,
                  google::protobuf::Message* resp,
                  Status* status) {
  DCHECK(status);
  CallWithCallback(path, req, resp, status, nullptr);
  Wait(status);
  return status->err;
}

void HttpRPC::Wait(Status* status) {
  client_->Wait(static_cast<HttpClient::Status*>(status));
}

void HttpRPC::CallWithCallback(
    const string& path,
    const google::protobuf::Message* req,
    google::protobuf::Message* resp,
    Status* status,
    OneshotClosure* callback) {
  std::unique_ptr<CallRequest> call_req(new CallRequest(req, status));
  if (IsCompressionEnabled()) {
    call_req->EnableCompression(
        options_.compression_level, options_.accept_encoding);
  }
  std::unique_ptr<Request> http_req = std::move(call_req);
  client_->InitHttpRequest(http_req.get(), "POST", path);
  std::unique_ptr<Response> http_resp(new CallResponse(resp, status));
  http_req->SetContentType(options_.content_type_for_protobuf);
  std::unique_ptr<CallData> call(
      new CallData(std::move(http_req), std::move(http_resp), callback));

  // Take pointers before call is moved to pass these addresses DoAsync.
  const auto* ptr_req = call->req();
  auto* ptr_resp = call->resp();

  VLOG(3) << "Call async " << call.get();
  OneshotClosure* done =
      NewCallback(this, &HttpRPC::CallDone, std::move(call));

  client_->DoAsync(ptr_req, ptr_resp,
                   static_cast<HttpClient::Status*>(status),
                   done);
  return;
}

void HttpRPC::CallDone(std::unique_ptr<CallData> call) {
  VLOG(3) << "CallDone " << call.get();
  if (call->resp()->status_code() != 200) {
    // Apiary returns 415 to reject Content-Encoding.
    if (call->resp()->status_code() == 400 ||
        call->resp()->status_code() == 415 ||
        call->resp()->result() == FAIL) {
      DisableCompression();
    }
  } else {
    EnableCompression(call->resp()->Header());
  }
  // destructor runs call->callback_
}

string HttpRPC::DebugString() const {
  AUTOLOCK(lock, &mu_);
  std::ostringstream ss;
  ss << "Compression:";
  if (compression_enabled_) {
    ss << "enabled";
  } else {
    ss << "disabled";
  }
  ss << std::endl;
  ss << "Accept-Encoding:" << options_.accept_encoding << std::endl;
  ss << "Content-Type:" << options_.content_type_for_protobuf << std::endl;
  ss << std::endl;
  return ss.str();
}

void HttpRPC::DumpToJson(Json::Value* json) const {
  client_->DumpToJson(json);
  AUTOLOCK(lock, &mu_);
  (*json)["compression"] = (compression_enabled_ ? "enabled" : "disabled");
  (*json)["accept_encoding"] = options_.accept_encoding;
  (*json)["content_type"] = options_.content_type_for_protobuf;
}

void HttpRPC::DumpStatsToProto(HttpRPCStats* stats) const {
  client_->DumpStatsToProto(stats);
}

void HttpRPC::DisableCompression() {
  AUTOLOCK(lock, &mu_);
  if (compression_enabled_)
    LOG(WARNING) << "Compression disabled";
  compression_enabled_ = false;
}

void HttpRPC::EnableCompression(absl::string_view header) {
  AUTOLOCK(lock, &mu_);
  absl::string_view::size_type accept_encoding =
      header.find("Accept-Encoding: deflate");
  if (accept_encoding != absl::string_view::npos) {
    if (!compression_enabled_)
      LOG(INFO) << "Compression enabled";
    compression_enabled_ = true;
  } else {
    compression_enabled_ = false;
  }
}

bool HttpRPC::IsCompressionEnabled() const {
  AUTOLOCK(lock, &mu_);
  if (!compression_enabled_)
    return false;
  if (options_.compression_level == 0)
    return false;
  return true;
}

HttpRPC::CallRequest::CallRequest(
    const google::protobuf::Message* req,
    HttpRPC::Status* status)
    : Request(req, status),
      compression_level_(0) {
}

string HttpRPC::CallRequest::CreateMessage() const {
  std::vector<string> headers;
  if (compression_level_ > 0 && accept_encoding_ != "" && req_) {
    string compressed;
    headers.push_back(HttpClient::Request::CreateHeader(
        "Accept-Encoding", accept_encoding_));
    SimpleTimer compression_timer;
    google::protobuf::io::StringOutputStream stream(&compressed);
    google::protobuf::io::GzipOutputStream::Options options;
    options.format = google::protobuf::io::GzipOutputStream::ZLIB;
    options.compression_level = compression_level_;
    google::protobuf::io::GzipOutputStream gzip_stream(&stream, options);
    req_->SerializeToZeroCopyStream(&gzip_stream);
    if (!gzip_stream.Close()) {
      LOG(ERROR) << "GzipOutputStream error:"
                 << gzip_stream.ZlibErrorMessage();
    } else if (compressed.size() > 1 && (compressed[1] >> 5 & 1)) {
      LOG(WARNING) << "response has FDICT, which should not be supported";
    } else {
      headers.push_back(
          HttpClient::Request::CreateHeader("Content-Encoding", "deflate"));
      status_->raw_req_size = gzip_stream.ByteCount();
      absl::string_view body(compressed);
      // Omit zlib header (since server assumes no zlib header).
      body.remove_prefix(2);
      return BuildMessage(headers, body);
    }
  } else {
    VLOG(1) << "compression unavailable.";
  }

  // Fallback if compression is not supported or failed.
  string raw_body;
  if (req_) {
    req_->SerializeToString(&raw_body);
  }
  status_->raw_req_size = raw_body.size();
  return BuildMessage(headers, raw_body);
}

void HttpRPC::CallResponse::ParseBody(
    google::protobuf::io::ZeroCopyInputStream* input) {
  if (resp_) {
    status_->raw_resp_size = resp_->ByteSize();
    if (!resp_->ParseFromZeroCopyStream(input)) {
      LOG(WARNING) << trace_id_ << " Parse response failed";
      err_message_ = "Parse response failed";
      result_ = FAIL;
      return;
    }
  }
  result_ = OK;
  return;
}

ExecServiceClient::ExecServiceClient(HttpRPC* http_rpc, string path)
    : http_rpc_(http_rpc), path_(std::move(path)) {}

void ExecServiceClient::ExecAsync(const ExecReq* req, ExecResp* resp,
                                  HttpClient::Status* status,
                                  OneshotClosure* callback) {
  http_rpc_->CallWithCallback(path_, req, resp, status, callback);
}

void ExecServiceClient::Exec(const ExecReq* req, ExecResp* resp,
                             HttpClient::Status* status) {
  http_rpc_->Call(path_, req, resp, status);
}

}  // namespace devtools_goma
