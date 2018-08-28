// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "http.h"

#ifndef _WIN32
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif
#include <time.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#include "absl/memory/memory.h"
#include "absl/strings/numbers.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "autolock_timer.h"
#include "callback.h"
#include "compiler_proxy_info.h"
#include "compiler_specific.h"
#include "compress_util.h"
#include "descriptor.h"
#include "env_flags.h"
#include "fileflag.h"
#include "glog/logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message.h"
MSVC_POP_WARNING()
#include "histogram.h"
#include "http_util.h"
#include "oauth2.h"
#include "oauth2_token.h"
#include "openssl_engine.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_stats.pb.h"
MSVC_POP_WARNING()
#include "rand_util.h"
#include "scoped_fd.h"
#include "simple_timer.h"
#include "socket_descriptor.h"
#include "socket_factory.h"
#include "socket_pool.h"
#include "time_util.h"
#include "tls_descriptor.h"
#include "util.h"
#include "worker_thread.h"
#include "zero_copy_stream_impl.h"

using std::string;

namespace devtools_goma {

namespace {

constexpr absl::Duration kDefaultThrottleTimeout = absl::Minutes(10);
constexpr absl::Duration kDefaultTimeout = absl::Minutes(15);

const size_t kMaxTrafficHistory = 120U;

const int kMaxQPS = 700;

constexpr absl::Duration kRampUpDuration = absl::Minutes(10);

constexpr int kMaxConnectionFailure = 5;

constexpr int kDefaultErrorThresholdPercent = 30;

bool IsFatalNetworkErrorCode(int status_code) {
  return status_code == 302 || status_code == 401 || status_code == 403;
}

absl::optional<absl::Time> CalculateEnabledFrom(
    int status_code, absl::optional<absl::Time> enabled_from) {
  constexpr absl::Duration kMinDisableDuration = absl::Minutes(10);
  constexpr absl::Duration kMaxDisableDuration = absl::Minutes(20);

  if (IsFatalNetworkErrorCode(status_code)) {
    // status code for blocking by dos server.
    const absl::Time enable_time =
        absl::Now() + RandomDuration(kMinDisableDuration, kMaxDisableDuration);
    if (!enabled_from.has_value() || enable_time > *enabled_from) {
      LOG(INFO) << "status=" << status_code
                << " extend enabled from: " << OptionalToString(enabled_from)
                << " to " << enable_time;
      enabled_from = enable_time;
    }
    return enabled_from;
  }
  // status_code == 200; success
  // status_code == 204; no response
  // status_code == 400; bad request (app error)
  // status_code == 408; timeout
  // status_code == 415; unsupported media type (disable compression)
  // status_code == 5xx; server error
  if ((status_code / 100) != 2) {
    // no update of enabled_from for other than 2xx.
    return enabled_from;
  }
  if (!enabled_from.has_value()) {
    return absl::nullopt;
  }
  const absl::Time now = absl::Now();
  if (now < *enabled_from) {
    // ramp up from now to now+kRampUpDurationSec.
    LOG(INFO) << "got 200 respose in enabled_from=" << *enabled_from
              << " start ramp up from " << now;
    enabled_from = now;
  } else if (*enabled_from <= now && now < *enabled_from + kRampUpDuration) {
    // nothing to do in ramp up period:
  } else if (*enabled_from + kRampUpDuration <= now) {
    LOG(INFO) << "got 200 response. finish ramp up period";
    enabled_from.reset();
  }
  return enabled_from;
}

// Randomizes backoff by subtracting 40%, so it returns
// [backoff*0.6, backoff].
absl::Duration RandomizeBackoff(absl::Duration backoff) {
  constexpr double kMinRandomRatio = 0.4;
  absl::Duration min_backoff = backoff * kMinRandomRatio;
  // Handle the special cases where:
  // - |backoff| is so small that |min_backoff| is rounded down to 0.
  // - |backoff| < 0.
  // These are handled after the following if block.
  if (backoff > min_backoff && min_backoff > absl::ZeroDuration()) {
    return RandomDuration(min_backoff, backoff);
  }
  return absl::Milliseconds(1);
}

}  // namespace

HttpClient::Options::Options()
    : dest_port(0), proxy_port(0),
      capture_response_header(false),
      use_ssl(false),
      socket_read_timeout(absl::Seconds(1)),
      min_retry_backoff(absl::Milliseconds(500)),
      max_retry_backoff(absl::Seconds(5)),
      fail_fast(false),
      network_error_threshold_percent(kDefaultErrorThresholdPercent),
      allow_throttle(true), reuse_connection(true) {
}

bool HttpClient::Options::InitFromURL(absl::string_view url) {
  size_t pos = url.find("://");
  if (pos == string::npos) {
    return false;
  }
  absl::string_view scheme = url.substr(0, pos);
  if (scheme == "http") {
    use_ssl = false;
    dest_port = 80;
  } else if (scheme == "https") {
    use_ssl = true;
    dest_port = 443;
  } else {
    return false;
  }
  absl::string_view hostport = url.substr(pos + 3);
  pos = hostport.find("/");
  if (pos != string::npos) {
    url_path_prefix = string(hostport.substr(pos));
    hostport = hostport.substr(0, pos);
  } else {
    url_path_prefix = "/";
  }
  pos = hostport.find(":");
  if (pos != string::npos) {
    dest_host_name = string(hostport.substr(0, pos));
    dest_port = atoi(string(hostport.substr(pos+1)).c_str());
  } else {
    dest_host_name = string(hostport);
  }
  return true;
}

string HttpClient::Options::SocketHost() const {
  if (!proxy_host_name.empty()) {
    return proxy_host_name;
  }
  return dest_host_name;
}

int HttpClient::Options::SocketPort() const {
  if (!proxy_host_name.empty()) {
    return proxy_port;
  }
  return dest_port;
}

string HttpClient::Options::RequestURL(absl::string_view path) const {
  std::ostringstream url;
  if ((dest_host_name != SocketHost()
       || dest_port != SocketPort())
      && !use_ssl) {
    // without SSL and with proxy, send request with absolute-form.
    url << "http://" << dest_host_name << ':' << dest_port;
  }
  url << url_path_prefix << path;
  url << extra_params;
  return url.str();
}

string HttpClient::Options::Host() const {
  if (!http_host_name.empty()) {
    return http_host_name;
  }
  if ((dest_host_name != SocketHost()
       || dest_port != SocketPort())
      && use_ssl) {
    return dest_host_name;
  }
  return SocketHost();
}

string HttpClient::Options::DebugString() const {
  std::ostringstream ss;
  ss << "dest=" << dest_host_name << ":" << dest_port;
  if (!http_host_name.empty())
    ss << " http_host=" << http_host_name;
  if (!url_path_prefix.empty())
    ss << " url_path_prefix=" << url_path_prefix;
  if (!proxy_host_name.empty())
    ss << " proxy=" << proxy_host_name << ":" << proxy_port;
  if (!extra_params.empty())
    ss << " extra=" << extra_params;
  if (!authorization.empty())
    ss << " authorization:enabled";
  if (!cookie.empty())
    ss << " cookie=" << cookie;
  if (oauth2_config.enabled())
    ss << " oauth2:enabled";
  if (!service_account_json_filename.empty())
    ss << " service_account:" << service_account_json_filename;
  if (!gce_service_account.empty())
    ss << " gce_service_account:" << gce_service_account;
  if (capture_response_header)
    ss << " capture_response_header";
  if (use_ssl)
    ss << " use_ssl";
  if (!ssl_extra_cert.empty())
    ss << " ssl_extra_cert=" << ssl_extra_cert;
  if (!ssl_extra_cert_data.empty())
    ss << " ssl_extra_cert_data:set";
  ss << " socket_read_timeout=" << socket_read_timeout;
  ss << " retry_backoff=" << min_retry_backoff << " .. " << max_retry_backoff;
  if (fail_fast) {
    ss << " fail_fast";
  }
  return ss.str();
}

void HttpClient::Options::ClearAuthConfig() {
  gce_service_account.clear();
  service_account_json_filename.clear();
  oauth2_config.clear();
  luci_context_auth.clear();
}

// This object is created when asynchronously waiting for
// HttpClient. The object is deleted by RunCallback,DoCallback.
class HttpClient::Task {
 public:
  Task(HttpClient* client,
       const HttpClient::Request* req,
       HttpClient::Response* resp,
       Status* status,
       WorkerThreadManager* wm,
       OneshotClosure* callback)
      : client_(client),
        req_(req),
        resp_(resp),
        status_(status),
        wm_(wm),
        thread_id_(wm_->GetCurrentThreadId()),
        descriptor_(nullptr),
        active_(false),
        close_state_(HttpClient::ERROR_CLOSE),
        auth_status_(OK),
        is_ping_(status_->trace_id == "ping"),
        callback_(callback) {
    if (status_->timeouts.empty()) {
      status_->timeouts.push_back(kDefaultTimeout);
    }
    client_->IncNumActive();
    resp_->SetRequestPath(req_->request_path());
    resp_->SetTraceId(status_->trace_id);
  }

  void Start() {
    CHECK(!status_->finished);
    CHECK(!active_);
    if (client_->failnow()) {
      status_->enabled = false;
      RunCallback(FAIL, "http fail now");
      return;
    }
    // TODO: rethink the way refreshing OAuth2 access token.
    // Refreshing OAuth2 access token is a bit complex operation, and
    // difficult to track the behavior.  Refactoring must be needed.
    if (auth_status_ == NEED_REFRESH) {
      const string& authorization = client_->GetOAuth2Authorization();
      if (authorization.empty()) {
        RunCallback(FAIL, "authorization not available");
        return;
      }
      cloned_req_ = req_->Clone();
      cloned_req_->SetAuthorization(authorization);
      auth_status_ = OK;
      req_ = cloned_req_.get();
      LOG(INFO) << status_->trace_id
                << " cloned HttpClient::Request to set authorization.";
    }
    if (client_->ShouldRefreshOAuth2AccessToken()) {
      LOG(INFO) << status_->trace_id
                << " authorization is not ready, going to run after refresh.";
      auth_status_ = NEED_REFRESH;
      client_->RunAfterOAuth2AccessTokenGetReady(
          wm_->GetCurrentThreadId(),
          NewCallback(this, &HttpClient::Task::Start));
      return;
    }
    const absl::Duration throttle_time = timer_.GetDuration();
    status_->throttle_time += throttle_time;
    const absl::Duration backoff = client_->TryStart();
    if (backoff > absl::ZeroDuration()) {
      if (status_->num_throttled == 0) {  // only increment first time.
        DCHECK_EQ(Status::INIT, status_->state);
        status_->state = Status::PENDING;
        client_->IncNumPending();
      }
      ++status_->num_throttled;
      if (status_->throttle_time > kDefaultThrottleTimeout) {
        LOG(WARNING) << status_->trace_id
                     << " Timeout in throttled. throttle_time="
                     << status_->throttle_time;
        RunCallback(ERR_TIMEOUT, "Time-out in throttled");
        return;
      }
      LOG(WARNING) << status_->trace_id
                   << " Throttled backoff=" << backoff
                   << " remaining="
                   << (kDefaultThrottleTimeout - status_->throttle_time);
      // TODO: might need to cancel this on shutdown?
      wm_->RunDelayedClosureInThread(
          FROM_HERE,
          wm_->GetCurrentThreadId(),
          backoff,
          NewCallback(this, &HttpClient::Task::Start));
      timer_.Start();
      return;
    }
    LOG_IF(INFO, status_->num_throttled > 0)
        << status_->trace_id << " http: Start throttled req. "
        << status_->num_throttled
        << " time=" << status_->throttle_time
        << " [last throttle=" << throttle_time << "]";
    if (status_->timeouts.empty()) {
      LOG(WARNING) << status_->trace_id << " Time-out in connect";
      RunCallback(ERR_TIMEOUT, "Time-out in connect");
      return;
    }

    // TODO: make connect async.
    descriptor_ = client_->NewDescriptor();
    if (descriptor_ == nullptr) {
      ++status_->num_connect_failed;
      // Note we do not retry if handling ping because its scenario
      // does not match what we expect.
      //
      // As written below, this code's goal is mitigating temporary
      // network failure while several requests are on-flight concurrently.
      // Since we usually run only one ping request, it does not meet
      // the scenario below.
      if (is_ping_ || status_->num_connect_failed > kMaxConnectionFailure) {
        RunCallback(FAIL, "Can't establish connection to server");
        return;
      }
      // Note that goal of this backoff and retry is mitigating a temporary
      // network failure suggested in: b/36575944#comment6
      // The scenario like:
      //   1. send request A
      //   2. send request B
      //   3. got error as response A or B
      //   4. send request C, need to connect -> fail. no address available
      //   5. got success as response A or B
      // (Considered elapsed time from Step 3 to Step 5 is expected to be small,
      //  say less than 1 second)
      //
      // Since we expect the address is marked as success again in Step 5.
      // we do not retry for long time. (e.g. 60 seconds to error address
      // become available in socket_pool.)
      const absl::Duration start_backoff = client_->GetRandomizedBackoff();
      LOG(WARNING) << status_->trace_id
                   << " Can't establish connection to server"
                   << " retry after backoff=" << start_backoff;
      // TODO: might need to cancel this on shutdown?
      wm_->RunDelayedClosureInThread(
          FROM_HERE,
          wm_->GetCurrentThreadId(),
          start_backoff,
          NewCallback(this, &HttpClient::Task::Start));
      timer_.Start();
      return;
    }
    if (status_->state == Status::PENDING) {
      client_->DecNumPending();
    }
    DCHECK(status_->state == Status::INIT || status_->state == Status::PENDING)
        << status_->trace_id << " state=" << status_->state;
    status_->state = Status::SENDING_REQUEST;

    resp_->Reset();
    active_ = true;
    status_->connect_success = true;
    const absl::Duration timeout = status_->timeouts.front();
    status_->timeouts.pop_front();
    timer_.Start();
    request_stream_ = req_->NewStream();
    if (!request_stream_) {
      LOG(WARNING) << status_->trace_id << " failed to create request stream";
      RunCallback(FAIL, "Failed to create request stream");
      return;
    }
    status_->req_build_time = timer_.GetDuration();

    descriptor_->NotifyWhenWritable(
        NewPermanentCallback(this, &HttpClient::Task::DoWrite));
    descriptor_->NotifyWhenTimedout(
        timeout, NewCallback(this, &HttpClient::Task::DoTimeout));
    timer_.Start();
  }

 private:
  enum AuthorizationStatus {
    OK,
    NEED_REFRESH,
  };
  ~Task() {
    CHECK(!active_);
  }

  void DoWrite() {
    if (!active_) {
      LOG(WARNING) << "Already finished?";
      RunCallback(FAIL, "Writable, but already inactive");
      return;
    }
    if (client_->failnow()) {
      status_->enabled = false;
      RunCallback(FAIL, "http fail now");
      return;
    }
    CHECK(descriptor_);
    VLOG(7) << "DoWrite " << descriptor_;
    const void* data = nullptr;
    int size = 0;
    if (!request_stream_->Next(&data, &size)) {
      // Request has been sent.
      DCHECK_EQ(Status::SENDING_REQUEST, status_->state);
      status_->req_size = request_stream_->ByteCount();
      status_->state = Status::REQUEST_SENT;
      descriptor_->StopWrite();
      wm_->RunClosureInThread(
          FROM_HERE,
          thread_id_,
          NewCallback(this, &HttpClient::Task::DoRequestDone),
          WorkerThread::PRIORITY_IMMEDIATE);
      return;
    }
    ssize_t write_size = descriptor_->Write(data, size);
    VLOG(3) << status_->trace_id << " DoWrite "
            << size << " -> " << write_size;
    if (write_size < 0 && descriptor_->NeedRetry()) {
      request_stream_->BackUp(size);
      return;
    }
    if (write_size <= 0) {
      LOG(WARNING) << status_->trace_id
                   << " Write failed "
                   << " write_size=" << write_size
                   << " err=" << descriptor_->GetLastErrorMessage();
      std::ostringstream err_message;
      err_message << status_->trace_id
                  << " Write failed write_size=" << write_size
                  << " @" << request_stream_->ByteCount()
                  << " : " << descriptor_->GetLastErrorMessage();
      RunCallback(FAIL, err_message.str());
      return;
    }
    request_stream_->BackUp(size - write_size);
    client_->IncWriteByte(write_size);
  }

  void DoRead() {
    if (!active_) {
      LOG(WARNING) << "Already finished?";
      RunCallback(FAIL, "Readable, but already inactive");
      return;
    }
    if (client_->failnow()) {
      status_->enabled = false;
      RunCallback(FAIL, "http fail now");
      return;
    }
    if (status_->state != Status::RECEIVING_RESPONSE) {
      DCHECK_EQ(Status::REQUEST_SENT, status_->state);
      status_->state = Status::RECEIVING_RESPONSE;
    }
    CHECK(descriptor_);
    char* buf;
    int buf_size;
    resp_->Buffer(&buf, &buf_size);
    ssize_t read_size = descriptor_->Read(buf, buf_size);
    VLOG(7) << "DoRead " << descriptor_
            << " buf_size=" << buf_size
            << " read_size=" << read_size;
    if (read_size < 0) {
      if (descriptor_->NeedRetry()) {
        return;
      }
      LOG(WARNING) << status_->trace_id
                   << " Read failed " << read_size
                   << " err=" << descriptor_->GetLastErrorMessage();
      std::ostringstream err_message;
      err_message << status_->trace_id
                  << " Read failed ret=" << read_size
                  << " @" << resp_->len()
                  << " of " << resp_->buffer_size()
                  << " : " << descriptor_->GetLastErrorMessage();
      err_message << " : received=" << resp_->Header();
      RunCallback(FAIL, err_message.str());
      return;
    }
    if (status_->wait_time == absl::ZeroDuration() && resp_->len() == 0) {
      status_->wait_time = timer_.GetDuration();
      timer_.Start();
      descriptor_->ChangeTimeout(client_->options().socket_read_timeout);
    }
    client_->IncReadByte(read_size);
    if (resp_->Recv(read_size)) {
      VLOG(1) << status_->trace_id << " response\n"
              << resp_->Header();
      status_->resp_recv_time = timer_.GetDuration();
      timer_.Start();
      resp_->Parse();
      status_->resp_parse_time = timer_.GetDuration();
      status_->resp_size = resp_->len();
      if (resp_->status_code() != 200 || resp_->result() == FAIL) {
        DCHECK_EQ(close_state_, HttpClient::ERROR_CLOSE);
        CaptureResponseHeader();
      } else {
        DCHECK_EQ(resp_->result(), OK);
        DCHECK_EQ(resp_->status_code(), 200);

        if (resp_->HasConnectionClose() ||
            !client_->options().reuse_connection) {
          close_state_ = HttpClient::NORMAL_CLOSE;
        } else {
          close_state_ = HttpClient::NO_CLOSE;
        }
      }
      status_->http_return_code = resp_->status_code();
      DCHECK_EQ(Status::RECEIVING_RESPONSE, status_->state);
      if (resp_->result() == OK || resp_->status_code() != 200) {
        status_->state = Status::RESPONSE_RECEIVED;
      }
      RunCallback(resp_->result(), resp_->err_message());
      return;
    }
    if (client_->options().capture_response_header && resp_->HasHeader()) {
      CaptureResponseHeader();
    }
    descriptor_->ChangeTimeout(client_->options().socket_read_timeout +
                               client_->EstimatedRecvTime(kNetworkBufSize));
  }

  void DoTimeout() {
    if (!active_) {
      LOG(WARNING) << "Already finished?";
      return;
    }
    if (client_->failnow()) {
      status_->enabled = false;
      RunCallback(FAIL, "http fail now");
      return;
    }
    if (status_->timeouts.empty()) {
      std::ostringstream err_message;
      err_message << "Timed out: ";
      if (request_stream_) {
        err_message << "sending request header "
                    << request_stream_->ByteCount()
                    << " " << timer_.GetDuration();
      } else if (resp_->len() == 0) {
        err_message << "waiting response "
                    << " " << timer_.GetDuration();
      } else {
        err_message << "receiving response "
                    << resp_->len()
                    << " of " << resp_->buffer_size()
                    << " " << timer_.GetDuration();
      }
      LOG(WARNING) << status_->trace_id << " " << err_message.str();
      RunCallback(ERR_TIMEOUT, err_message.str());
      return;
    }
    descriptor_->StopRead();
    descriptor_->StopWrite();
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        NewCallback(this, &HttpClient::Task::DoRetry),
        WorkerThread::PRIORITY_MED);
  }

  void RunCallback(int err, const string& err_message) {
    VLOG(2) << status_->trace_id
            << " RunCallback"
            << " err=" << err
            << " msg=" << err_message;
    if (descriptor_) {
      descriptor_->StopRead();
      descriptor_->StopWrite();
    }
    active_ = false;
    status_->err = err;
    status_->err_message = err_message;

    if (status_->state == Status::PENDING) {
      client_->DecNumPending();
    }

    // We MUST use lower priority than Descriptor to ensure the TLS write
    // closure stopped.
    wm_->RunClosureInThread(
        FROM_HERE,
        thread_id_,
        NewCallback(this, &HttpClient::Task::DoCallback),
        WorkerThread::PRIORITY_MED);
  }

  void DoRetry() {
    LOG(INFO) << status_->trace_id << " DoRetry ";
    if (!active_)
      return;
    Descriptor* d = descriptor_;
    descriptor_ = nullptr;
    client_->ReleaseDescriptor(d, HttpClient::ERROR_CLOSE);
    active_ = false;
    request_stream_.reset();
    resp_->Reset();
    ++status_->num_retry;
    Start();
  }

  void DoRequestDone() {
    VLOG(3) << status_->trace_id << " DoWrite " << " done";
    if (!active_)
      return;
    status_->req_send_time = timer_.GetDuration();
    request_stream_.reset();
    descriptor_->ClearWritable();
    descriptor_->NotifyWhenReadable(
        NewPermanentCallback(this, &HttpClient::Task::DoRead));
    timer_.Start();
  }

  void DoCallback() {
    VLOG(3) << status_->trace_id << " DoCallback"
            << " close_state=" << close_state_;
    CHECK(!active_);
    Descriptor* d = descriptor_;
    descriptor_ = nullptr;
    // once callback_ is called, it is not safe to touch status_.
    status_->finished = true;
    // Since status for ping would be updated in
    // UpdateHealthStatusMessageForPing, we do not need to update it here.
    // (b/26701852)
    if (!is_ping_) {
      client_->UpdateStats(*status_);
    } else {
      LOG(INFO) << "We will not update status for ping.";
    }
    OneshotClosure* callback = callback_;
    callback_ = nullptr;
    if (callback)
      callback->Run();
    client_->ReleaseDescriptor(d, close_state_);
    client_->DecNumActive();
    delete this;
  }

  void CaptureResponseHeader() {
    if (!status_->response_header.empty())
      return;
    status_->response_header = string(resp_->Header());
  }

  HttpClient* client_;
  const HttpClient::Request* req_;
  std::unique_ptr<HttpClient::Request> cloned_req_;
  HttpClient::Response* resp_;
  Status* status_;
  WorkerThreadManager* wm_;
  WorkerThread::ThreadId thread_id_;
  Descriptor* descriptor_;

  bool active_;
  HttpClient::ConnectionCloseState close_state_;
  AuthorizationStatus auth_status_;

  std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> request_stream_;

  const bool is_ping_;

  SimpleTimer timer_;

  // Callback that is called when RPC is received and has completed.
  OneshotClosure* callback_;

  DISALLOW_COPY_AND_ASSIGN(Task);
};

/* static */
absl::string_view HttpClient::Status::StateName(State state) {
  switch (state) {
    case INIT:
      return "INIT";
    case PENDING:
      return "PENDING";
    case SENDING_REQUEST:
      return "SENDING_REQUEST";
    case REQUEST_SENT:
      return "REQUEST_SENT";
    case RECEIVING_RESPONSE:
      return "RECEIVING_RESPONSE";
    case RESPONSE_RECEIVED:
      return "RESPONSE_RECEIVED";
    default:
      return "invalid HttpClient::Status::State";
  }
}

HttpClient::Status::Status()
    : state(Status::INIT),
      timeout_should_be_http_error(true),
      connect_success(false),
      finished(false),
      err(0),
      enabled(true),
      http_return_code(0),
      req_size(0),
      resp_size(0),
      raw_req_size(0),
      raw_resp_size(0),
      num_retry(0),
      num_throttled(0),
      num_connect_failed(0) {
}

string HttpClient::Status::DebugString() const {
  std::ostringstream ss;
  ss << "state=" << state
     << " timeout_should_be_http_error=" << timeout_should_be_http_error
     << " connect_success=" << connect_success
     << " finished=" << finished
     << " err=" << err
     << " http_return_code=" << http_return_code
     << " req_size=" << req_size
     << " resp_size=" << resp_size
     << " raw_req_size=" << raw_req_size
     << " raw_resp_size=" << raw_resp_size
     << " throttle_time=" << throttle_time
     << " pending_time=" << pending_time
     << " req_build_time=" << req_build_time
     << " req_send_time=" << req_send_time
     << " wait_time=" << wait_time
     << " resp_recv_time=" << resp_recv_time
     << " resp_parse_time=" << resp_parse_time
     << " num_retry=" << num_retry
     << " num_throttled=" << num_throttled
     << " num_connect_failed=" << num_connect_failed;
  return ss.str();
}

HttpClient::TrafficStat::TrafficStat()
    : read_byte(0), write_byte(0), query(0), http_err(0) {
}

/* static */
std::unique_ptr<SocketFactory> HttpClient::NewSocketFactoryFromOptions(
    const Options& options) {
  return std::unique_ptr<SocketFactory>(
      new SocketPool(options.SocketHost(), options.SocketPort()));
}

std::unique_ptr<TLSEngineFactory> HttpClient::NewTLSEngineFactoryFromOptions(
    const Options& options) {
  if (options.use_ssl) {
    std::unique_ptr<OpenSSLEngineCache> ssl_engine_fact(new OpenSSLEngineCache);
    if (!options.ssl_extra_cert.empty())
      ssl_engine_fact->AddCertificateFromFile(options.ssl_extra_cert);
    if (!options.ssl_extra_cert_data.empty())
      ssl_engine_fact->AddCertificateFromString(options.ssl_extra_cert_data);
    ssl_engine_fact->SetHostname(options.dest_host_name);
    if (!options.proxy_host_name.empty()) {
      ssl_engine_fact->SetProxy(options.proxy_host_name, options.proxy_port);
    }
    ssl_engine_fact->SetCRLMaxValidDuration(options.ssl_crl_max_valid_duration);
    return std::unique_ptr<TLSEngineFactory>(std::move(ssl_engine_fact));
  }
  return nullptr;
}

HttpClient::HttpClient(std::unique_ptr<SocketFactory> socket_factory,
                       std::unique_ptr<TLSEngineFactory> tls_engine_factory,
                       const Options& options,
                       WorkerThreadManager* wm)
    : options_(options),
      tls_engine_factory_(std::move(tls_engine_factory)),
      socket_pool_(std::move(socket_factory)),
      wm_(wm),
      health_status_("initializing"),
      shutting_down_(false),
      bad_status_num_in_recent_http_(0),
      network_error_status_(options.network_error_margin),
      num_query_(0),
      num_active_(0),
      total_pending_(0),
      peak_pending_(0),
      num_pending_(0),
      num_http_retry_(0),
      num_http_timeout_(0),
      num_http_error_(0),
      total_write_byte_(0),
      total_read_byte_(0),
      num_writable_(0),
      num_readable_(0),
      read_size_(new Histogram),
      write_size_(new Histogram),
      total_resp_byte_(0),
      total_resp_time_(absl::ZeroDuration()),
      ping_http_return_code_(-1),
      traffic_history_closure_id_(kInvalidPeriodicClosureId),
      retry_backoff_(options.min_retry_backoff),
      num_network_error_(0),
      num_network_recovered_(0) {
  LOG(INFO) << options_.DebugString();
  CHECK_GT(retry_backoff_, absl::ZeroDuration());
  CHECK_LT(options.min_retry_backoff, options.max_retry_backoff);
  read_size_->SetName("read size distribution");
  write_size_->SetName("write size distribution");
  if (!options_.authorization.empty()) {
    CHECK(options_.authorization.find_first_of("\r\n") == string::npos)
        << "authorization must not contain CR LF:" << options_.authorization;
  }
  if (!options_.cookie.empty()) {
    CHECK(options_.cookie.find_first_of("\r\n") == string::npos)
        << "cookie must not contain CR LF:" << options_.cookie;
  }
  LOG_IF(ERROR, !socket_pool_->IsInitialized())
      << "socket pool is not initialized yet.";
  traffic_history_.push_back(TrafficStat());

  traffic_history_closure_id_ = wm_->RegisterPeriodicClosure(
      FROM_HERE, absl::Seconds(1), NewPermanentCallback(
          this, &HttpClient::UpdateTrafficHistory));

  if (options_.use_ssl) {
    DCHECK(tls_engine_factory_.get() != nullptr);
    socket_pool_->SetObserver(tls_engine_factory_.get());
  }
  HttpClient::Options oauth2_options;
  oauth2_options.proxy_host_name = options.proxy_host_name;
  oauth2_options.proxy_port = options.proxy_port;
  oauth2_options.gce_service_account = options.gce_service_account;
  oauth2_options.service_account_json_filename =
      options.service_account_json_filename;
  oauth2_options.oauth2_config = options.oauth2_config;
  oauth2_options.luci_context_auth = options.luci_context_auth;
  oauth_refresh_task_ = OAuth2AccessTokenRefreshTask::New(
      wm_, oauth2_options);
}

HttpClient::~HttpClient() {
  {
    AUTOLOCK(lock, &mu_);
    shutting_down_ = true;
    LOG(INFO) << "wait all tasks num_active=" << num_active_;
    while (num_active_ > 0)
      cond_.Wait(&mu_);
  }
  if (oauth_refresh_task_.get()) {
    oauth_refresh_task_->Shutdown();
    oauth_refresh_task_->Wait();
  }
  if (traffic_history_closure_id_ != kInvalidPeriodicClosureId) {
    wm_->UnregisterPeriodicClosure(traffic_history_closure_id_);
    traffic_history_closure_id_ = kInvalidPeriodicClosureId;
  }
  LOG(INFO) << "HttpClient terminated.";
}

void HttpClient::InitHttpRequest(
    Request* req, const string& method, const string& path) const {
  req->Init(method, path, options_);
  const string& auth = GetOAuth2Authorization();
  if (!auth.empty()) {
    req->SetAuthorization(auth);
    LOG_IF(WARNING, !options_.authorization.empty())
        << "authorization option is given but ignored.";
  }
}

void HttpClient::Do(const Request* req, Response* resp, Status* status) {
  DCHECK(status);
  DCHECK(wm_);
  DoAsync(req, resp, status, nullptr);
  Wait(status);
}

void HttpClient::DoAsync(
    const Request* req, Response* resp,
    Status* status, OneshotClosure* callback) {
  if (failnow()) {
    status->enabled = false;
    status->connect_success = false;
    status->finished = true;
    status->err = FAIL;
    status->err_message = "http disabled";
    status->http_return_code = 403;
    // once callback_ is called, it is not safe to touch status.
    if (callback)
      callback->Run();
    return;
  }

  DCHECK(wm_) << "There isn't any worker thread to send to";
  Task* task = new Task(this, req, resp, status, wm_, callback);
  task->Start();
  return;
}

void HttpClient::Wait(Status* status) {
  while (!status->finished) {
    CHECK(wm_->Dispatch());
  }
}

void HttpClient::Shutdown() {
  {
    AUTOLOCK(lock, &mu_);
    LOG(INFO) << "shutdown";
    shutting_down_ = true;
    health_status_ = "shutting down";
  }
  if (oauth_refresh_task_.get()) {
    oauth_refresh_task_->Shutdown();
  }
}

bool HttpClient::shutting_down() const {
  AUTOLOCK(lock, &mu_);
  return shutting_down_;
}

Descriptor* HttpClient::NewDescriptor() {
  ScopedSocket fd(socket_pool_->NewSocket());
  // Note that unlike our past implementation, even on seeing previous network
  // error we can get at least one socket if getaddrinfo succeeds.
  // Thus, invalid fd means no address found by getaddrinfo.
  if (!fd.valid()) {
    {
      AUTOLOCK(lock, &mu_);
      NetworkErrorDetectedUnlocked();
    }
    return nullptr;
  }
  if (options_.use_ssl) {
    TLSEngine *engine = tls_engine_factory_->NewTLSEngine(fd.get());
    TLSDescriptor::Options tls_desc_options;
    if (!options_.proxy_host_name.empty()) {
      tls_desc_options.use_proxy = true;
      tls_desc_options.dest_host_name = options_.dest_host_name;
      tls_desc_options.dest_port = options_.dest_port;
    }
    TLSDescriptor* d = new TLSDescriptor(
        wm_->RegisterSocketDescriptor(std::move(fd),
                                      WorkerThread::PRIORITY_MED),
        engine, tls_desc_options, wm_);
    d->Init();
    return d;
  }
  return wm_->RegisterSocketDescriptor(std::move(fd),
                                       WorkerThread::PRIORITY_MED);
}

void HttpClient::ReleaseDescriptor(
    Descriptor* d, ConnectionCloseState close_state) {
  if (d == nullptr)
    return;

  bool reuse_socket = (close_state == NO_CLOSE) && d->CanReuse();
  SocketDescriptor* sd = d->socket_descriptor();
  DCHECK(!reuse_socket || !sd->IsClosed())
    << "should not reuse the socket if it has already been closed."
    << " fd=" << sd->fd()
    << " reuse_socket=" << reuse_socket
    << " close_state=" << close_state
    << " is_closed=" << sd->IsClosed()
    << " can_reuse=" << d->CanReuse();
  if (options_.use_ssl) {
    TLSDescriptor* tls_desc = static_cast<TLSDescriptor*>(d);
    delete tls_desc;
  }
  ScopedSocket fd(wm_->DeleteSocketDescriptor(sd));
  VLOG(3) << "Release fd=" << fd.get()
          << " reuse_socket=" << reuse_socket
          << " close_state=" << close_state;
  if (fd.valid()) {
    if (reuse_socket) {
      socket_pool_->ReleaseSocket(std::move(fd));
    } else {
      socket_pool_->CloseSocket(std::move(fd), close_state == ERROR_CLOSE);
    }
  }
}

bool HttpClient::failnow() const {
  AUTOLOCK(lock, &mu_);
  if (shutting_down_) {
    return true;
  }
  if (!enabled_from_.has_value()) {
    return false;
  }
  return absl::Now() < *enabled_from_;
}

int HttpClient::ramp_up() const {
  AUTOLOCK(lock, &mu_);
  if (!enabled_from_.has_value()) {
    return 100;
  }
  const absl::Time now = absl::Now();
  if (now < *enabled_from_) {
    return 0;
  }
  return std::min<int>(100, (now - *enabled_from_) * 100 / kRampUpDuration);
}

string HttpClient::GetHealthStatusMessage() const {
  AUTOLOCK(lock, &mu_);
  return health_status_;
}

void HttpClient::UpdateStatusCodeHistoryUnlocked() {
  const absl::Time now = absl::Now();

  while (!recent_http_status_code_.empty() &&
         recent_http_status_code_.front().first < now - absl::Seconds(3)) {
    if (recent_http_status_code_.front().second != 200) {
      --bad_status_num_in_recent_http_;
    }
    recent_http_status_code_.pop_front();
  }
}

void HttpClient::AddStatusCodeHistoryUnlocked(int status_code) {
  UpdateStatusCodeHistoryUnlocked();

  const absl::Time now = absl::Now();
  if (status_code != 200) {
    ++bad_status_num_in_recent_http_;
  }
  recent_http_status_code_.emplace_back(now, status_code);
}

bool HttpClient::IsHealthyRecently() {
  AUTOLOCK(lock, &mu_);

  UpdateStatusCodeHistoryUnlocked();

  return bad_status_num_in_recent_http_ <=
      recent_http_status_code_.size() *
      options_.network_error_threshold_percent / 100;
}

bool HttpClient::IsHealthy() const {
  AUTOLOCK(lock, &mu_);
  return health_status_ == "ok";
}

string HttpClient::GetAccount() {
  if (oauth_refresh_task_.get() == nullptr) {
    return "";
  }
  return oauth_refresh_task_->GetAccount();
}

bool HttpClient::GetOAuth2Config(OAuth2Config* config) const {
  if (oauth_refresh_task_.get() == nullptr) {
    return false;
  }
  return oauth_refresh_task_->GetOAuth2Config(config);
}

bool HttpClient::SetOAuth2Config(const OAuth2Config& config) {
  if (oauth_refresh_task_.get() == nullptr) {
    return false;
  }
  if (oauth_refresh_task_->SetOAuth2Config(config)) {
    AUTOLOCK(lock, &mu_);
    // if disabled by 401 error, could try now with new oauth2 config.
    LOG(INFO) << "new oauth2 config: reset enabled_from_="
              << OptionalToString(enabled_from_)
              << " to 0";
    enabled_from_.reset();
    return true;
  }
  return false;
}

string HttpClient::DebugString() const {
  AUTOLOCK(lock, &mu_);

  std::ostringstream ss;
  ss << "Status:" << health_status_ << std::endl;
  ss << "Remote host: " << socket_pool_->DestName();
  if (!options_.url_path_prefix.empty()) {
    ss << " " << options_.url_path_prefix;
  }
  if (!options_.extra_params.empty()) {
    ss << ": " << options_.extra_params;
  }
  if (!options_.proxy_host_name.empty()) {
    ss << " to "
       << "http://" << options_.dest_host_name << ":" << options_.dest_port;
  }
  ss << std::endl;
  ss << "User-Agent: " << kUserAgentString << std::endl;
  ss << "SocketPool: " << socket_pool_->DebugString() << std::endl;
  if (!options_.http_host_name.empty())
    ss << "Host: " << options_.http_host_name << std::endl;
  if (!options_.authorization.empty())
    ss << "Authorization: enabled" << std::endl;
  if (!options_.cookie.empty())
    ss << "Cookie: " << options_.cookie << std::endl;
  if (options_.oauth2_config.enabled()) {
    ss << "OAuth2: enabled";
    if (!options_.service_account_json_filename.empty())
      ss << " service_account:" << options_.service_account_json_filename;
    if (!options_.gce_service_account.empty())
      ss << " gce service_account:" << options_.gce_service_account;
    ss << std::endl;
  }
  ss << std::endl;
  if (options_.capture_response_header)
    ss << "Capture response header: enabled" << std::endl;

  ss << std::endl;

  ss << "http status:" << std::endl;
  for (const auto& iter : num_http_status_code_) {
    ss << " " << iter.first << ": " << iter.second
       << " (" << (iter.second * 100.0 / num_query_) << "%)" << std::endl;
  }
  ss << " Retry: " << num_http_retry_;
  if (num_query_ > 0)
    ss << " (" << (num_http_retry_ * 100.0 / num_query_) << "%)";
  ss << std::endl;
  ss << " Timeout: " << num_http_timeout_;
  if (num_query_ > 0)
    ss << " (" << (num_http_timeout_ * 100.0 / num_query_) << "%)";
  ss << std::endl;
  ss << " Error: " << num_http_error_;
  if (num_query_ > 0)
    ss << " (" << (num_http_error_ * 100.0 / num_query_) << "%)";
  ss << std::endl;
  ss << " Pending: " << total_pending_;
  if (num_query_ > 0)
    ss << " (" << (total_pending_ * 100.0 / num_query_) << "%)";
  ss << " peek " << peak_pending_;
  ss << std::endl;

  ss << std::endl;
  ss << "Backoff: " << retry_backoff_ << std::endl;
  if (enabled_from_.has_value()) {
    ss << "Disabled for " << (*enabled_from_ - absl::Now()) << std::endl;
  }

  ss << std::endl;
  ss << "Write: " << total_write_byte_ << "bytes "
     << num_writable_ << "calls" << std::endl;
  ss << "Read: " << total_read_byte_ << "bytes "
     << num_readable_ << "calls "
     << "(" << total_resp_byte_ << "bytes in " << total_resp_time_ << ")";
  ss << std::endl;
  ss << std::endl;
  ss << write_size_->DebugString() << std::endl;
  ss << read_size_->DebugString() << std::endl;

  ss << std::endl;
  if (options_.use_ssl) {
    ss << "SSL enabled" << std::endl;
    ss << "Certificate(s) and CRLs:" << std::endl;
    ss << tls_engine_factory_->GetCertsInfo();
  } else {
    ss << "SSL disabled" << std::endl;
  }
  ss << std::endl;

  ss << "Network: " << std::endl
     << " Error Count: " << num_network_error_ << std::endl
     << " Recovered Count: " << num_network_recovered_ << std::endl;

  return ss.str();
}

void HttpClient::DumpToJson(Json::Value* json) const {
  AUTOLOCK(lock, &mu_);
  (*json)["health_status"] = health_status_;
  if (!options_.http_host_name.empty()) {
    (*json)["http_host_name"] = options_.http_host_name;
  }
  if (!options_.url_path_prefix.empty()) {
    (*json)["url_path_prefix"] = options_.url_path_prefix;
  }
  if (!options_.extra_params.empty()) {
    (*json)["extra_params"] = options_.extra_params;
  }
  (*json)["user_agent"] = kUserAgentString;
  (*json)["socket_pool"] = socket_pool_->DebugString();
  (*json)["authorization"] = (
      options_.authorization.empty() ? "none" : "enabled");
  (*json)["cookie"] = options_.cookie;
  (*json)["oauth2"] = (!options_.oauth2_config.enabled() ? "none" : "enabled");
  (*json)["capture_response_header"] = (
      options_.capture_response_header ? "enabled" : "disabled");
  (*json)["ssl"] = (options_.use_ssl ? "enabled" : "disabled");
  if (!options_.ssl_extra_cert.empty()) {
    (*json)["ssl_extra_cert"] = options_.ssl_extra_cert;
  }
  if (!options_.ssl_extra_cert_data.empty()) {
    (*json)["ssl_extra_cert_data"] = "set";
  }
  (*json)["socket_read_timeout_sec"] =
      Json::Int64(absl::ToInt64Seconds(options_.socket_read_timeout));
  (*json)["num_query"] = num_query_;
  (*json)["num_active"] = num_active_;
  (*json)["num_http_retry"] = num_http_retry_;
  (*json)["num_http_timeout"] = num_http_timeout_;
  (*json)["num_http_error"] = num_http_error_;
  (*json)["write_byte"] = Json::Int64(total_write_byte_);
  (*json)["read_byte"] = Json::Int64(total_read_byte_);
  (*json)["num_writable"] = Json::Int64(num_writable_);
  (*json)["num_readable"] = Json::Int64(num_readable_);
  (*json)["resp_byte"] = Json::Int64(total_resp_byte_);
  (*json)["resp_time"] =
      Json::Int64(absl::ToInt64Milliseconds(total_resp_time_));
  {
    TrafficHistory::const_reverse_iterator iter = traffic_history_.rbegin();
    ++iter;
    if (iter != traffic_history_.rend()) {
      (*json)["read_bps"] = iter->read_byte;
      (*json)["write_bps"] = iter->write_byte;
    } else {
      (*json)["read_bps"] = 0;
      (*json)["write_bps"] = 0;
    }
  }

  double byte_max = 0.0;
  double q_max = 0.0;
  std::vector<double> read_value;
  std::vector<double> write_value;
  std::vector<double> qps;
  std::vector<double> http_err;
  for (size_t i = 0; i < kMaxTrafficHistory - traffic_history_.size(); ++i) {
    read_value.push_back(-1.0);
    write_value.push_back(-1.0);
    qps.push_back(-1.0);
    http_err.push_back(-1.0);
  }
  for (TrafficHistory::const_iterator iter = traffic_history_.begin();
       iter != traffic_history_.end();
       ++iter) {
    byte_max = std::max<double>(iter->read_byte, byte_max);
    read_value.push_back(static_cast<double>(iter->read_byte));
    byte_max = std::max<double>(iter->write_byte, byte_max);
    write_value.push_back(static_cast<double>(iter->write_byte));
    q_max = std::max<double>(iter->query, q_max);
    qps.push_back(static_cast<double>(iter->query));
    q_max = std::max<double>(iter->http_err, q_max);
    http_err.push_back(static_cast<double>(iter->http_err));
  }
  byte_max = byte_max * 1.1;
  q_max = q_max * 1.1;

}

void HttpClient::DumpStatsToProto(HttpRPCStats* stats) const {
  AUTOLOCK(lock, &mu_);
  stats->set_ping_status_code(ping_http_return_code_);
  if (ping_round_trip_time_.has_value()) {
    stats->set_ping_round_trip_time_ms(DurationToIntMs(*ping_round_trip_time_));
  }
  stats->set_query(num_query_);
  stats->set_active(num_active_);
  stats->set_retry(num_http_retry_);
  stats->set_timeout(num_http_timeout_);
  stats->set_error(num_http_error_);
  stats->set_network_error(num_network_error_);
  stats->set_network_recovered(num_network_recovered_);
  stats->set_current_pending(num_pending_);
  stats->set_peak_pending(peak_pending_);
  stats->set_total_pending(total_pending_);
  for (const auto& iter : num_http_status_code_) {
    HttpRPCStats_HttpStatus* http_status = stats->add_status_code();
    http_status->set_status_code(iter.first);
    http_status->set_count(iter.second);
  }
}

int HttpClient::UpdateHealthStatusMessageForPing(
    const Status& status, absl::optional<absl::Duration> round_trip_time) {
  LOG(INFO) << "Ping status:"
            << " http_return_code=" << status.http_return_code
            << " throttle_time=" << status.throttle_time
            << " pending_time=" << status.pending_time
            << " req_build_time=" << status.req_build_time
            << " req_send_time=" << status.req_send_time
            << " wait_time=" << status.wait_time
            << " resp_recv_time=" << status.resp_recv_time
            << " resp_parse_time=" << status.resp_parse_time
            << " round_trip_time=" << OptionalToString(round_trip_time);

  AUTOLOCK(lock, &mu_);
  AddStatusCodeHistoryUnlocked(status.http_return_code);

  if (shutting_down_) {
    health_status_ = "shutting down";
    ping_http_return_code_ = 0;
    return ping_http_return_code_;
  }

  // Under race condition of initial ping, good ping status could be
  // overridden by bad ping status.  (b/26701852)
  if (ping_http_return_code_ == 200 && status.http_return_code != 200) {
    LOG(INFO) << "We do not update status with bad status."
              << " ping_http_return_code_=" << ping_http_return_code_
              << " status.http_return_code=" << status.http_return_code;
    return ping_http_return_code_;
  }
  if (!status.finished) {
    health_status_ = "error: ping no response";
    ping_http_return_code_ = 408; // status timeout.
    return ping_http_return_code_;
  }
  if (!status.connect_success) {
    health_status_ = "error: failed to connect to backend servers";
    ping_http_return_code_ = 0;
    return ping_http_return_code_;
  }
  if (status.err == ERR_TIMEOUT) {
    health_status_ = "error: timed out to send request to backend servers";
    ping_http_return_code_ = 408;
    return ping_http_return_code_;
  }
  ping_http_return_code_ = status.http_return_code;
  ping_round_trip_time_ = round_trip_time;
  const string running = options_.fail_fast ? "error:" : "running:";
  if (status.http_return_code != 200) {
    int status_code = status.http_return_code;
    enabled_from_ =
        CalculateEnabledFrom(status.http_return_code, enabled_from_);
    if (IsFatalNetworkErrorCode(status.http_return_code)) {
      NetworkErrorDetectedUnlocked();
    }
    if (status.http_return_code == 401) {
      // TODO: make it error, so goma_ctl abort "start"?
      health_status_ = running + " access to backend servers was rejected.";
    } else if (status.http_return_code == 302
               || status.http_return_code == 403) {
      std::ostringstream ss;
      ss << running << " access to backend servers was blocked:"
         << status.http_return_code;
      health_status_ = ss.str();
    } else if (status.http_return_code == 0 && status.err < 0) {
      health_status_ = running + " failed to send request to backend servers";
      status_code = 500;
    } else {
      std::ostringstream ss;
      ss << running << " access to backend servers was failed:"
         << status.http_return_code;
      health_status_ = ss.str();
    }
    return status_code;
  }
  health_status_ = "ok";
  return status.http_return_code;
}

absl::Duration HttpClient::EstimatedRecvTime(size_t bytes) {
  AUTOLOCK(lock, &mu_);

  if (total_resp_byte_ == 0)
    return absl::ZeroDuration();

  // total_resp_time_ is in milliseconds.
  return total_resp_time_ * bytes / total_resp_byte_;
}

/* static */
absl::Duration HttpClient::GetNextBackoff(
    const Options& options, absl::Duration prev_backoff, bool in_error) {
  // Multiply factor used in chromium.
  // URLRequestThrottlerEntry::kDefaultMultiplyFactor
  // in net/url_request/url_request_throttler_entry.cc
  constexpr double kBackoffBase = 1.4;
  CHECK_GT(prev_backoff, absl::ZeroDuration());
  absl::Duration uncapped_backoff = prev_backoff;
  if (in_error) {
    uncapped_backoff *= kBackoffBase;
    return std::min(uncapped_backoff, options.max_retry_backoff);
  }
  uncapped_backoff /= kBackoffBase;
  return std::max(uncapped_backoff, options.min_retry_backoff);
}

void HttpClient::UpdateBackoffUnlocked(bool in_error) {
  const absl::Duration orig_backoff = retry_backoff_;
  CHECK_GT(orig_backoff, absl::ZeroDuration());
  retry_backoff_ = GetNextBackoff(options_, retry_backoff_, in_error);
  if (in_error) {
    LOG(INFO) << "UpdateBackoff error "
              << orig_backoff << " -> " << retry_backoff_;
  } else {
    VLOG(2) << "UpdateBackoff ok " << orig_backoff << " -> " << retry_backoff_;
  }
}

string HttpClient::GetOAuth2Authorization() const {
  if (!oauth_refresh_task_.get()) {
    return "";
  }
  // TODO: disable http on error.
  return oauth_refresh_task_->GetAuthorization();
}

bool HttpClient::ShouldRefreshOAuth2AccessToken() const {
  if (!oauth_refresh_task_.get()) {
    return false;
  }
  return oauth_refresh_task_->ShouldRefresh();
}

void HttpClient::RunAfterOAuth2AccessTokenGetReady(
    WorkerThread::ThreadId thread_id, OneshotClosure* closure) {

  CHECK(oauth_refresh_task_.get());
  oauth_refresh_task_->RunAfterRefresh(thread_id, closure);
}

absl::Duration HttpClient::GetRandomizedBackoff() const {
  return RandomizeBackoff(retry_backoff_);
}

absl::Duration HttpClient::TryStart() {
  AUTOLOCK(lock, &mu_);
  if ((traffic_history_.back().http_err > 0 ||
       traffic_history_.back().query >= kMaxQPS) &&
      options_.allow_throttle) {
    LOG(WARNING) << "Throttled. queries=" << traffic_history_.back().query
                 << " err=" << traffic_history_.back().http_err
                 << " retry_backoff_=" << retry_backoff_;
    return GetRandomizedBackoff();
  }
  ++num_query_;
  ++traffic_history_.back().query;
  return absl::ZeroDuration();
}

void HttpClient::IncNumActive() {
  AUTOLOCK(lock, &mu_);
  ++num_active_;
}

void HttpClient::DecNumActive() {
  AUTOLOCK(lock, &mu_);
  --num_active_;
  DCHECK_GE(num_active_, 0);
  if (num_active_ == 0)
    cond_.Signal();
}

void HttpClient::WaitNoActive() {
  AUTOLOCK(lock, &mu_);
  while (num_active_ > 0)
    cond_.Wait(&mu_);
}

void HttpClient::IncNumPending() {
  AUTOLOCK(lock, &mu_);
  ++num_pending_;
  ++total_pending_;
  peak_pending_ = std::max(peak_pending_, num_pending_);
}

void HttpClient::DecNumPending() {
  AUTOLOCK(lock, &mu_);
  --num_pending_;
  DCHECK_GE(num_pending_, 0);
}

void HttpClient::IncReadByte(int n) {
  AUTOLOCK(lock, &mu_);
  traffic_history_.back().read_byte += n;
  total_read_byte_ += n;
  ++num_readable_;
  read_size_->Add(n);
}

void HttpClient::IncWriteByte(int n) {
  AUTOLOCK(lock, &mu_);
  traffic_history_.back().write_byte += n;
  total_write_byte_ += n;
  ++num_writable_;
  write_size_->Add(n);
}

void HttpClient::UpdateStats(const Status& status) {
  AUTOLOCK(lock, &mu_);

  AddStatusCodeHistoryUnlocked(status.http_return_code);

  ++num_http_status_code_[status.http_return_code];
  if (status.err != OK) {
    UpdateBackoffUnlocked(true);
    if (status.err == ERR_TIMEOUT) {
      ++num_http_timeout_;
      if (status.timeout_should_be_http_error) {
        ++traffic_history_.back().http_err;
      }
    } else {
      ++num_http_error_;
      if (status.err == FAIL && status.http_return_code == 408) {
        if (status.timeout_should_be_http_error) {
          ++traffic_history_.back().http_err;
        }
      } else {
        ++traffic_history_.back().http_err;
      }
    }
  } else {
    UpdateBackoffUnlocked(false);
  }
  enabled_from_ = CalculateEnabledFrom(status.http_return_code, enabled_from_);
  if (IsFatalNetworkErrorCode(status.http_return_code)) {
    NetworkErrorDetectedUnlocked();
  }
  num_http_retry_ += status.num_retry;
  total_resp_byte_ += status.resp_size;
  total_resp_time_ += status.resp_recv_time;

  // clear network_error_started_time_ in 2xx response.
  if (status.http_return_code / 100 == 2) {
    NetworkRecoveredUnlocked();
  }
}

void HttpClient::UpdateTrafficHistory() {
  AUTOLOCK(lock, &mu_);
  if (!shutting_down_) {
    if (traffic_history_.back().query > 0 &&
        total_resp_time_ > absl::ZeroDuration()) {
      if (traffic_history_.back().http_err == 0) {
        if (health_status_ != "ok") {
          LOG(INFO) << "Update health status:" << health_status_ << " to ok";
        }
        health_status_ = "ok";
      } else {
        const string running = options_.fail_fast ? "error:" : "running:";
        if (health_status_ == "ok") {
          LOG(WARNING) << "Update health status: ok to "
                       << running
                       << " had some http errors from backend servers";
        }
        health_status_ = running + " had some http errors from backend servers";
      }
    }
  }

  traffic_history_.push_back(TrafficStat());
  if (traffic_history_.size() >= kMaxTrafficHistory) {
    traffic_history_.pop_front();
  }
}

void HttpClient::NetworkErrorDetectedUnlocked() {
  // set network error started time if it is not set.
  const absl::Time now = absl::Now();

  if (!network_error_status_.OnNetworkErrorDetected(now)) {
    LOG(INFO)
        << "Network error continues from "
        << OptionalToString(network_error_status_.NetworkErrorStartedTime());
    return;
  }

  LOG(INFO) << "Network error started: time=" << now;
  ++num_network_error_;

  if (monitor_.get())
    monitor_->OnNetworkErrorDetected();
}

void HttpClient::NetworkRecoveredUnlocked() {
  const absl::Time now = absl::Now();
  absl::optional<absl::Time> network_error_started_time =
      network_error_status_.NetworkErrorStartedTime();

  if (!network_error_status_.OnNetworkRecovered(now)) {
    LOG_IF(INFO, network_error_started_time.has_value())
        << "Waiting network recover until "
        << *network_error_status_.NetworkErrorUntil();
    return;
  }

  LOG(INFO)
      << "Network recovered"
      << " started=" << OptionalToString(network_error_started_time)
      << " recovered=" << now
      << " duration="
      << OptionalToString(
             network_error_started_time.has_value() ?
                 absl::optional<absl::Duration>(
                    now - *network_error_started_time) :
                 absl::nullopt);
  ++num_network_recovered_;
  if (monitor_.get())
    monitor_->OnNetworkRecovered();
}

void HttpClient::SetMonitor(
    std::unique_ptr<HttpClient::NetworkErrorMonitor> monitor) {
  AUTOLOCK(lock, &mu_);
  monitor_ = std::move(monitor);
}

absl::optional<absl::Time> HttpClient::NetworkErrorStartedTime() const {
  AUTOLOCK(lock, &mu_);
  return network_error_status_.NetworkErrorStartedTime();
}

HttpClient::Request::Request()
    : content_type_("application/octet-stream") {
}

HttpClient::Request::~Request() {
}

void HttpClient::Request::Init(
    const string& method, const string& path,
    const HttpClient::Options& options) {
  SetMethod(method);
  SetRequestPath(options.RequestURL(path));
  SetHost(options.Host());
  if (!options.authorization.empty()) {
    SetAuthorization(options.authorization);
  }
  if (!options.cookie.empty()) {
    SetCookie(options.cookie);
  }
}

void HttpClient::Request::SetMethod(const string& method) {
  method_ = method;
}

void HttpClient::Request::SetRequestPath(const string& path) {
  request_path_ = path;
}

void HttpClient::Request::SetHost(const string& host) {
  host_ = host;
}

void HttpClient::Request::SetContentType(const string& content_type) {
  content_type_ = content_type;
}

void HttpClient::Request::SetAuthorization(const string& authorization) {
  authorization_ = authorization;
}

void HttpClient::Request::SetCookie(const string& cookie) {
  cookie_ = cookie;
}

void HttpClient::Request::AddHeader(const string& key, const string& value) {
  headers_.push_back(CreateHeader(key, value));
}

/* static */
string HttpClient::Request::CreateHeader(
    absl::string_view key, absl::string_view value) {
  std::ostringstream line;
  line << key << ": " << value;
  return line.str();
}

string HttpClient::Request::BuildHeader(
    const std::vector<string>& headers,
    int content_length) const {
  std::ostringstream msg;
  msg << method_ << " " << request_path_ << " HTTP/1.1\r\n";
  if (host_ != "") {
    msg << kHost << ": " << host_ << "\r\n";
  }
  msg << kUserAgent << ": " << kUserAgentString << "\r\n";
  msg << kContentType << ": " << content_type_ << "\r\n";
  if (content_length >= 0) {
    msg << kContentLength << ": " << content_length << "\r\n";
  }
  if (authorization_ != "") {
    msg << kAuthorization << ": " << authorization_ << "\r\n";
  }
  if (cookie_ != "") {
    msg << kCookie << ": " << cookie_ << "\r\n";
  }
  bool chunked = false;
  for (const auto& header : headers_) {
    msg << header << "\r\n";
    if (absl::StartsWith(header, absl::StrCat(kTransferEncoding, ":")) &&
        absl::StrContains(header, "chunked")) {
      chunked = true;
    }
  }
  for (const auto& header : headers) {
    msg << header << "\r\n";
    if (absl::StartsWith(header, absl::StrCat(kTransferEncoding, ":")) &&
        absl::StrContains(header, "chunked")) {
      chunked = true;
    }
  }
  if (content_length < 0) {
    CHECK(chunked) << "content-length is not give, but not chunked encoding";
  }
  // TODO: request_stream_ should provide chunked-body.
  msg << "\r\n";
  VLOG(1) << "request\n" << msg.str();
  return msg.str();
}

HttpRequest::HttpRequest() {
}

HttpRequest::~HttpRequest() {
}

void HttpRequest::SetBody(const string& body) {
  body_ = body;
}

std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>
HttpRequest::NewStream() const {
  std::vector<std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>> s;
  s.reserve(2);
  s.push_back(absl::make_unique<StringInputStream>(
      BuildHeader(std::vector<string>(), body_.size())));
  s.push_back(absl::make_unique<google::protobuf::io::ArrayInputStream>(
      body_.data(), body_.size()));
  return absl::make_unique<ChainedInputStream>(std::move(s));
}

std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>
HttpFileUploadRequest::NewStream() const {
  ScopedFd fd(ScopedFd::OpenForRead(filename_));
  if (!fd.valid()) {
    return nullptr;
  }
  std::vector<std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>> s;
  s.reserve(2);
  // TODO: use chunked encoding for body and no require size_ ?
  s.push_back(absl::make_unique<StringInputStream>(
      BuildHeader(std::vector<string>(), size_)));

  // pass ownership of fd to body.
  std::unique_ptr<ScopedFdInputStream> body(
      absl::make_unique<ScopedFdInputStream>(std::move(fd)));
  s.push_back(std::move(body));
  return absl::make_unique<ChainedInputStream>(std::move(s));
}

std::unique_ptr<HttpClient::Request> HttpFileUploadRequest::Clone() const {
  return absl::make_unique<HttpFileUploadRequest>(*this);
}

// GetConentEncoding reports EncodingType specified in header.
// not http_util because it depends lib/compress_util EncodingType.
static EncodingType GetContentEncoding(absl::string_view header) {
  absl::string_view content_encoding =
      ExtractHeaderField(header, kContentEncoding);
  return GetEncodingFromHeader(content_encoding);
}

HttpClient::Response::Response()
    // to initialize in class definition, http.h needs to include
    // scoped_fd.h for FAIL.
    : result_(FAIL) {
}

HttpClient::Response::~Response() {
}

void HttpClient::Response::SetRequestPath(const string& path) {
  request_path_ = path;
}

void HttpClient::Response::SetTraceId(const string& trace_id) {
  trace_id_ = trace_id;
}

void HttpClient::Response::Reset() {
  result_ = FAIL;
  len_ = 0;
  body_offset_ = 0;
  status_code_ = 0;
  body_ = nullptr;
}

bool HttpClient::Response::HasHeader() const {
  return body_offset_ > 0;
}

absl::string_view HttpClient::Response::Header() const {
  if (body_offset_ > 0) {
    return absl::string_view(buffer_.data(), body_offset_);
  }
  absl::string_view::size_type header_size = buffer_.find("\r\n\r\n");
  if (header_size == string::npos) {
    header_size = len_;
  }
  return absl::string_view(buffer_.data(), header_size);
}

void HttpClient::Response::Buffer(char** buf, int* buf_size) {
  if (!body_) {
    *buf_size = buffer_.size() - len_;
    if (*buf_size < kNetworkBufSize / 2) {
      buffer_.resize(buffer_.size() + kNetworkBufSize);
    }
    *buf = &buffer_[len_];
    *buf_size = buffer_.size() - len_;
  } else {
    body_->Next(buf, buf_size);
  }
  CHECK_GT(*buf_size, 0)
      << " response len=" << len_
      << " size=" << buffer_.size()
      << " body_offset=" << body_offset_;
}

bool HttpClient::Response::Recv(int r) {
  if (body_) {
    return BodyRecv(r);
  }
  // header
  if (r == 0) {  // EOF
    LOG(WARNING) << trace_id_ <<
        " not received a header but connection closed by a peer.";
    err_message_ = "connection closed before receiving a header.";
    result_ = FAIL;
    body_offset_ = len_;
    return true;
  }
  len_ += r;
  absl::string_view resp(buffer_.data(), len_);
  size_t content_length = string::npos;
  bool is_chunked = false;
  if (!ParseHttpResponse(resp, &status_code_, &body_offset_,
                         &content_length,
                         &is_chunked)) {
    // still reading header.
    return false;
  }
  VLOG(2) << "header ready " << status_code_
          << " offset=" << body_offset_
          << " content_length=" << content_length
          << " is_chunked=" << is_chunked
          << " len=" << len_;
  // Apiary returns 204 No Content for SaveLog.
  if (status_code_ == 204 && body_offset_ == len_) {
    // Go to next step quickly since Status 204 has nothing to parse.
    result_ = OK;
    return true;
  }
  if (status_code_ != 200) {
    // heder found and error code.
    LOG(WARNING) << trace_id_ << " read "
                 << " http=" << status_code_
                 << " path=" << request_path_
                 << " Details:" << resp;
    std::ostringstream err;
    err << "Got HTTP error:" << status_code_;
    err_message_ = err.str();
    result_ = FAIL;
    return true;
  }
  if (body_offset_ == len_ && content_length == 0) {
    // nothing to parse for body.
    result_ = OK;
    return true;
  }
  EncodingType encoding = GetContentEncoding(Header());
  body_ = NewBody(content_length, is_chunked, encoding);
  if (!body_) {
    LOG(WARNING) << trace_id_ << " failed to create body "
                 << " content_length=" << content_length
                 << " is_chunked=" << is_chunked
                 << " encoding=" << GetEncodingName(encoding);
    err_message_ = "filed to create body";
    result_ = FAIL;
    return true;
  }
  if (body_offset_ < len_) {
    // header buffer_ has head of body.
    absl::string_view body(buffer_.data(), len_);
    body.remove_prefix(body_offset_);
    VLOG(3) << trace_id_ << " body " << body.size() << " after header";
    do {
      char* buf = nullptr;
      int buf_size = 0;
      body_->Next(&buf, &buf_size);
      if (body.size() <= buf_size) {
        buf_size = body.size();
      }
      memcpy(buf, body.data(), buf_size);
      body.remove_prefix(buf_size);
      if (BodyRecv(buf_size)) {
        return true;
      }
    } while (!body.empty());
  }
  return false;
}

bool HttpClient::Response::BodyRecv(int r) {
  VLOG(3) << trace_id_ << " body receive=" << r;
  switch (body_->Process(r)) {
    case Body::State::Error:
      if (r == 0) {
        LOG(WARNING) << trace_id_
                     << " connection closed before receiving all data at "
                     << body_->ByteCount();
        err_message_ =
            absl::StrCat("connection closed before receiving all data at ",
                         body_->ByteCount());
        result_ = FAIL;
        return true;
      }
      LOG(WARNING) << trace_id_
                   << " body receive failed @" << body_->ByteCount()
                   << " size=" << r;
      err_message_ =
          absl::StrCat("body receive failed at ", body_->ByteCount());
      result_ = FAIL;
      return true;

    case Body::State::Ok:
      CHECK_GE(r, 0);
      VLOG(3) << trace_id_ << " received full content";
      return true;

    case Body::State::Incomplete:
      CHECK_GT(r, 0);
      VLOG(3) << trace_id_ << " need more data";
      return false;
  }
}

bool HttpClient::Response::HasConnectionClose() const {
  return ExtractHeaderField(Header(), kConnection) == "close";
}

void HttpClient::Response::Parse() {
  if (result_ == OK) {
    return;
  }
  if (!err_message_.empty()) {
    return;
  }
  if (!body_) {
    return;
  }
  ParseBody();
}

HttpResponse::Body::Body(size_t content_length,
                         bool is_chunked,
                         EncodingType encoding_type)
    : content_length_(content_length),
      encoding_type_(encoding_type) {
  if (is_chunked) {
    chunk_parser_ = absl::make_unique<HttpChunkParser>();
  }
}

void HttpResponse::Body::Next(char** buf, int* buf_size) {
  size_t allocated = buffer_.size() * kNetworkBufSize;
  if (len_ == allocated) {
    VLOG(3) << "allocate resp body buffer len=" << len_;
    buffer_.emplace_back(absl::make_unique<char[]>(kNetworkBufSize));
    allocated += kNetworkBufSize;
  }
  *buf_size = allocated - len_;
  *buf = buffer_.back().get() + len_ % kNetworkBufSize;
  CHECK_GT(*buf_size, 0)
      << " body len=" << len_
      << " allocated=" << allocated;
}

HttpClient::Response::Body::State
HttpResponse::Body::Process(int data_size) {
  VLOG(3) << "body process " << data_size
          << " len=" << len_
          << " content_length=" << content_length_
          << " is_chunked=" << (chunk_parser_ ? true : false);
  if (data_size < 0) {
    return State::Error;
  }
  if (data_size == 0) {  // EOF
    if (!chunk_parser_) {
      if (content_length_ == string::npos) {
        VLOG(3) << "content finished with EOF";
        return State::Ok;
      }
      if (content_length_ == len_) {
        // empty body's case
        VLOG(3) << "empty content";
        return State::Ok;
      }
    }
    VLOG(3) << "unexpected EOF at " << len_;
    return State::Error;
  }
  DCHECK_LE(data_size, kNetworkBufSize);
  CHECK_LE(len_ + data_size, buffer_.size() * kNetworkBufSize);
  absl::string_view data(buffer_.back().get() + len_ % kNetworkBufSize,
                         data_size);
  len_ += data_size;
  if (chunk_parser_) {
    if (!chunk_parser_->Parse(data, &chunks_)) {
      VLOG(3) << "failed to parse chunk";
      return State::Error;
    }
    if (!chunk_parser_->done()) {
      VLOG(3) << "chunk not fully received yet";
      return State::Incomplete;
    }
    VLOG(3) << "all chunk finished";
    return State::Ok;
  }
  chunks_.emplace_back(data);
  if (content_length_ == string::npos) {
    // read until EOF.
    return State::Incomplete;
  }
  if (len_ > content_length_) {
    LOG(WARNING) << "received extra data?? len=" << len_
                 << " content_length=" << content_length_;
    return State::Error;
  }
  if (len_ == content_length_) {
    VLOG(3) << "content finished at " << content_length_;
    return State::Ok;
  }
  return State::Incomplete;
}

std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>
HttpResponse::Body::ParsedStream() const {
  std::vector<std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>>
      chunk_streams;
  for (const auto& chunk : chunks_) {
    chunk_streams.push_back(
        absl::make_unique<google::protobuf::io::ArrayInputStream>(
            chunk.data(), chunk.size()));
  }
  std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> input
      = absl::make_unique<ChainedInputStream>(std::move(chunk_streams));

  switch (encoding_type_) {
    case EncodingType::DEFLATE:
      return absl::make_unique<InflateInputStream>(std::move(input));
    case EncodingType::GZIP:
      return absl::make_unique<GzipInputStream>(std::move(input));
    case EncodingType::LZMA2:
#ifdef ENABLE_LZMA
      return absl::make_unique<LZMAInputStream>(std::move(input));
#else
      LOG(WARNING) << "unsuported encoding: lzma2.  need ENABLE_LZMA";
      return nullptr;
#endif
    default:
      VLOG(1) << "encoding: not specified";
      break;
  }
  return input;
}

HttpResponse::HttpResponse() {
}

HttpResponse::~HttpResponse() {
}

HttpClient::Response::Body* HttpResponse::NewBody(
    size_t content_length, bool is_chunked, EncodingType encoding_type) {
  response_body_ =
      absl::make_unique<Body>(content_length, is_chunked, encoding_type);
  return response_body_.get();
}

void HttpResponse::ParseBody() {
  std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> input =
      response_body_->ParsedStream();
  if (input == nullptr) {
    err_message_ = "failed to create parsed stream";
    result_ = FAIL;
    return;
  }
  std::ostringstream ss;
  const void* buffer;
  int size;
  while (input->Next(&buffer, &size)) {
    ss.write(static_cast<const char*>(buffer), size);
  }
  parsed_body_ = ss.str();
  result_ = OK;
}

HttpFileDownloadResponse::Body::Body(
    ScopedFd&& fd,
    size_t content_length, bool is_chunked, EncodingType encoding_type)
    : fd_(std::move(fd)),
      content_length_(content_length),
      encoding_type_(encoding_type) {
  if (is_chunked) {
    chunk_parser_ = absl::make_unique<HttpChunkParser>();
  }
  LOG_IF(FATAL, encoding_type_ != EncodingType::NO_ENCODING)
      << "unsupported encoding:"  << GetEncodingName(encoding_type);
  // TODO: z_streamp for inflate?
}

void HttpFileDownloadResponse::Body::Next(char** buf, int* buf_size) {
  *buf = &buf_[0];
  *buf_size = kNetworkBufSize;
}

HttpClient::Response::Body::State
HttpFileDownloadResponse::Body::Process(int data_size) {
  VLOG(3) << "body download process " << data_size
          << " len=" << len_
          << " content_length=" << content_length_
          << " is_chunked=" << (chunk_parser_ ? true : false);
  if (data_size < 0) {
    return State::Error;
  }
  if (data_size == 0) {  // EOF
    if (!chunk_parser_) {
      if (content_length_ == string::npos) {
        VLOG(3) << "content finished with EOF";
        return Close();
      }
      if (content_length_ == len_) {
        // empty body case
        VLOG(3) << "empty content";
        return Close();
      }
    }
    LOG(ERROR) << "unexpected EOF at " << len_;
    return State::Error;
  }
  DCHECK_LE(data_size, kNetworkBufSize);
  absl::string_view data(buf_, data_size);
  len_ += data_size;
  if (chunk_parser_) {
    std::vector<absl::string_view> chunks;
    if (!chunk_parser_->Parse(data, &chunks)) {
      LOG(ERROR) << "failed to parse chunk at " << len_;
      return State::Error;
    }
    for (const auto& chunk : chunks) {
      if (!Write(chunk)) {
        return State::Error;
      }
    }
    if (!chunk_parser_->done()) {
      VLOG(3) << "chunk not fully received yet";
      return State::Incomplete;
    }
    VLOG(3) << "all chunk finihsed";
    return Close();
  }
  if (!Write(data)) {
    return State::Error;
  }
  if (content_length_ == string::npos) {
    // read until EOF.
    return State::Incomplete;
  }
  if (len_ > content_length_) {
    LOG(WARNING) << "received extra data?? len=" << len_
                 << " content_length=" << content_length_;
    return State::Error;
  }
  if (len_ == content_length_) {
    VLOG(3) << "content finished at " << content_length_;
    return Close();
  }
  return State::Incomplete;
}

bool HttpFileDownloadResponse::Body::Write(absl::string_view data) {
  ssize_t n = fd_.Write(data.data(), data.size());
  if (n != data.size()) {
    LOG(WARNING) << "partial write " << n << " != " << data.size();
    return false;
  }
  return true;
}

HttpClient::Response::Body::State
HttpFileDownloadResponse::Body::Close() {
  if (!fd_.Close()) {
    LOG(WARNING) << "close error for downloading to file";
    return State::Error;
  }
  return State::Ok;
}

HttpFileDownloadResponse::HttpFileDownloadResponse(
    std::string filename, int mode)
    : filename_(std::move(filename)),
      mode_(mode) {
}

HttpFileDownloadResponse::~HttpFileDownloadResponse() {
}

HttpClient::Response::Body*
HttpFileDownloadResponse::NewBody(
    size_t content_length, bool is_chunked,
    EncodingType encoding_type) {
  if (encoding_type != EncodingType::NO_ENCODING) {
    // TODO: support deflate, lzma2
    LOG(ERROR) << "unsupported encoding is requested:"
               << GetEncodingName(encoding_type);
    return nullptr;
  }
  ScopedFd fd(ScopedFd::Create(filename_, mode_));
  if (!fd.valid()) {
    LOG(ERROR) << "failed to create " << filename_;
    return nullptr;
  }
  response_body_ =
      absl::make_unique<Body>(std::move(fd),
                              content_length, is_chunked, encoding_type);
  return response_body_.get();
}

void HttpFileDownloadResponse::ParseBody() {
  result_ = OK;
}

bool HttpClient::NetworkErrorStatus::OnNetworkErrorDetected(absl::Time now) {
  if (error_started_time_.has_value()) {
    error_until_ = now + error_recover_margin_;
    return false;
  }

  error_started_time_ = now;
  error_until_ = now + error_recover_margin_;

  return true;
}

bool HttpClient::NetworkErrorStatus::OnNetworkRecovered(absl::Time now) {
  if (!error_started_time_.has_value()) {
    return false;
  }

  // We don't consider the network is recovered until error_until_.
  if (error_until_.has_value() && now < *error_until_) {
    return false;
  }

  // Here, we consider the network error is really recovered.
  error_started_time_.reset();
  error_until_.reset();
  return true;
}

}  // namespace devtools_goma
