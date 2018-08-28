// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_HTTP_RPC_H_
#define DEVTOOLS_GOMA_CLIENT_HTTP_RPC_H_

#include <memory>
#include <string>

#include <json/json.h>

#include "basictypes.h"
#include "gtest/gtest_prod.h"
#include "lockhelper.h"
#include "http.h"

using std::string;

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace devtools_goma {

class ExecReq;
class ExecResp;
class HttpRPCStats;
class OneshotClosure;
class SimpleTimer;
class WorkerThreadManager;

// HttpRPC is a RPC system that uses protobuf over HttpClient.
class HttpRPC {
 public:
  struct Options {
    Options();
    int compression_level;
    bool start_compression;
    string accept_encoding;
    string content_type_for_protobuf;

    string DebugString() const;
  };
  // TODO: HttpRPC specific status?
  typedef HttpClient::Status Status;

  // It doesn't take ownership of client.
  HttpRPC(HttpClient* client, const Options& options);
  ~HttpRPC();

  // Ping sends ping message.
  // This is in HttpRPC, not in HttpClient, because we might need to
  // call via RPC for Apiary case.
  int Ping(WorkerThreadManager* wm, const string& path,
           Status *status);

  // Call calls a RPC synchronously.
  int Call(const string& path,
           const google::protobuf::Message* req,
           google::protobuf::Message* resp,
           Status* status);

  // CallWithCallback initiates a RPC asynchronously.
  // Caller have ownership of req, resp and status until RPC is finished.
  // Once RPC is finished, callback is called (if callback != NULL), or
  // status->finished becomes true (if callback == NULL).
  void CallWithCallback(
      const string& path,
      const google::protobuf::Message* req,
      google::protobuf::Message* resp,
      Status* status,
      OneshotClosure* callback);

  // Wait waits for a RPC initiated by CallWithCallback with callback=NULL.
  void Wait(Status* status);

  string DebugString() const;

  void DumpToJson(Json::Value* json) const;
  void DumpStatsToProto(HttpRPCStats* stats) const;

  HttpClient* client() const { return client_; }
  const Options& options() const { return options_; }

 private:
  FRIEND_TEST(HttpRPCTest, EnableCompression);
  class Request;
  class CallRequest;
  class Response;
  class CallResponse;
  class CallData;

  void DoPing(string path, Status* status);
  void PingDone(std::unique_ptr<Status> status,
                std::unique_ptr<SimpleTimer> timer);

  void CallDone(std::unique_ptr<CallData> call);

  void DisableCompression();
  void EnableCompression(absl::string_view header);
  // Initial request_encoding_type is determined by options_.accept_encoding.
  // Once it received response, use server's Accept-Encoding: response header.
  // Prefers gzip to deflate.  no lzma2 support yet.
  EncodingType request_encoding_type() const;
  bool IsCompressionEnabled() const;

  HttpClient* client_;
  const Options options_;
  mutable Lock mu_;
  EncodingType request_encoding_type_ GUARDED_BY(mu_);

  DISALLOW_COPY_AND_ASSIGN(HttpRPC);
};

class ExecServiceClient {
 public:
  ExecServiceClient(HttpRPC* http_rpc, string path);
  virtual ~ExecServiceClient() = default;

  ExecServiceClient(const ExecServiceClient&) = delete;
  ExecServiceClient& operator=(const ExecServiceClient&) = delete;

  virtual void ExecAsync(const ExecReq* req, ExecResp* resp,
                         HttpClient::Status* status, OneshotClosure* callback);

  virtual void Exec(const ExecReq* req, ExecResp* resp,
                    HttpClient::Status* status);

 private:
  HttpRPC* http_rpc_;
  const string path_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_HTTP_RPC_H_
