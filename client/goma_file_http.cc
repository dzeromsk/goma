// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "goma_file_http.h"

#include <sstream>
#include <utility>

#include "absl/memory/memory.h"
#include "compiler_specific.h"
#include "glog/logging.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "goma_file.h"
#include "http_rpc.h"
#include "lockhelper.h"

namespace {

template<typename Req, typename Resp>
class HttpTask : public devtools_goma::FileServiceClient::AsyncTask<Req, Resp> {
 public:
  HttpTask(devtools_goma::FileServiceHttpClient* file_service,
           string path,
           const string& trace_id)
      : file_service_(file_service),
        http_(file_service->http()),
        path_(std::move(path)) {
    std::ostringstream ss;
    if (!trace_id.empty()) {
      ss << trace_id << " ";
    }
    ss << "AsyncFileTask";
    status_.trace_id = ss.str();
    status_.finished = true;  // allow to destruct this without Run().
  }
  ~HttpTask() override {
    http_->Wait(&status_);
  }

  void Run() override {
    status_.finished = false;
    Req* req =
        devtools_goma::FileServiceClient::AsyncTask<Req, Resp>::mutable_req();
    Resp* resp =
        devtools_goma::FileServiceClient::AsyncTask<Req, Resp>::mutable_resp();
    http_->CallWithCallback(path_, req, resp, &status_, nullptr);
  }

  void Wait() override {
    http_->Wait(&status_);
    file_service_->AddHttpRPCStatus(status_);
  }
  bool IsSuccess() const override { return status_.err == 0; }  // OK

 private:
  devtools_goma::FileServiceHttpClient* file_service_;
  devtools_goma::HttpRPC* http_;
  string path_;
  devtools_goma::HttpRPC::Status status_;

  // disallow copy and assign
  HttpTask(const HttpTask&);
  void operator=(const HttpTask&);
};

}  // namespace

namespace devtools_goma {

FileServiceHttpClient::FileServiceHttpClient(HttpRPC* http,
                                             string store_path,
                                             string lookup_path,
                                             MultiFileStore* multi_file_store)
    : http_(http),
      store_path_(std::move(store_path)),
      lookup_path_(std::move(lookup_path)),
      num_rpc_(0),
      multi_file_store_(multi_file_store) {}

FileServiceHttpClient::~FileServiceHttpClient() {
}

std::unique_ptr<FileServiceHttpClient>
FileServiceHttpClient::WithRequesterInfoAndTraceId(
    const RequesterInfo& requester_info,
    const string& trace_id) const {
  std::unique_ptr<FileServiceHttpClient> cloned(
      new FileServiceHttpClient(http_, store_path_, lookup_path_,
                                multi_file_store_));
  cloned->requester_info_ = absl::make_unique<RequesterInfo>();
  *cloned->requester_info_ = requester_info;
  cloned->trace_id_ = trace_id;
  return cloned;
}

std::unique_ptr<FileServiceClient::AsyncTask<StoreFileReq, StoreFileResp>>
FileServiceHttpClient::NewAsyncStoreFileTask() {
  return std::unique_ptr<
    FileServiceClient::AsyncTask<StoreFileReq, StoreFileResp>>(
        new HttpTask<StoreFileReq, StoreFileResp>(
            this, store_path_, trace_id_));
}

std::unique_ptr<FileServiceClient::AsyncTask<LookupFileReq, LookupFileResp>>
FileServiceHttpClient::NewAsyncLookupFileTask() {
  return std::unique_ptr<
    FileServiceClient::AsyncTask<LookupFileReq, LookupFileResp>>(
        new HttpTask<LookupFileReq, LookupFileResp>(
            this, lookup_path_, trace_id_));
}

bool FileServiceHttpClient::StoreFile(
    const StoreFileReq* req, StoreFileResp* resp) {
  HttpRPC::Status status;
  std::ostringstream ss;
  if (!trace_id_.empty()) {
    ss << trace_id_ << " ";
  }
  ss << "StoreFile " << req->blob_size() << "blobs";
  status.trace_id = ss.str();
  multi_file_store_->StoreFile(&status, req, resp, nullptr);
  http_->Wait(&status);
  AddHttpRPCStatus(status);
  return status.err == 0;
}

bool FileServiceHttpClient::LookupFile(
    const LookupFileReq* req, LookupFileResp* resp) {
  HttpRPC::Status status;
  std::ostringstream ss;
  if (!trace_id_.empty()) {
    ss << trace_id_ << " ";
  }
  ss << "LookupFile " << req->hash_key_size() << "keys";
  status.trace_id = ss.str();
  status.timeout_should_be_http_error = false;
  bool ret = !http_->Call(lookup_path_, req, resp, &status);
  AddHttpRPCStatus(status);
  return ret;
}

void FileServiceHttpClient::AddHttpRPCStatus(const HttpRPC::Status& status) {
  ++num_rpc_;
  status_.req_size += status.req_size;
  status_.resp_size += status.resp_size;
  status_.raw_req_size += status.raw_req_size;
  status_.raw_resp_size += status.raw_resp_size;
  status_.req_build_time += status.req_build_time;
  status_.req_send_time += status.req_send_time;
  status_.wait_time += status.wait_time;
  status_.resp_recv_time += status.resp_recv_time;
  status_.resp_parse_time += status.resp_parse_time;
}

}  // namespace devtools_goma
