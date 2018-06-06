// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMA_FILE_HTTP_H_
#define DEVTOOLS_GOMA_CLIENT_GOMA_FILE_HTTP_H_

#include <memory>
#include <string>

#include "goma_file.h"
#include "http_rpc.h"
#include "multi_http_rpc.h"

namespace devtools_goma {

class Closure;
class RequesterInfo;

class FileServiceHttpClient : public FileServiceClient {
 public:
  // It doesn't take ownership of http and multi_file_store.
  FileServiceHttpClient(HttpRPC* http,
                        string store_path,
                        string lookup_path,
                        MultiFileStore* multi_file_store);
  ~FileServiceHttpClient() override;

  // This function doesn't clone |status_|.
  std::unique_ptr<FileServiceHttpClient> WithRequesterInfoAndTraceId(
      const RequesterInfo& requester_info, const string& trace_id) const;

  std::unique_ptr<AsyncTask<StoreFileReq, StoreFileResp>>
      NewAsyncStoreFileTask() override;
  std::unique_ptr<AsyncTask<LookupFileReq, LookupFileResp>>
      NewAsyncLookupFileTask() override;

  bool StoreFile(const StoreFileReq* req, StoreFileResp* resp) override;
  bool LookupFile(const LookupFileReq* req, LookupFileResp* resp) override;

  HttpRPC* http() { return http_; }

  void AddHttpRPCStatus(const HttpRPC::Status& status);
  int num_rpc() const { return num_rpc_; }
  const HttpRPC::Status& http_rpc_status() const { return status_; }

  const MultiFileStore* multi_file_store() const {
    return multi_file_store_;
  }

 private:
  HttpRPC* http_;
  const string store_path_;
  const string lookup_path_;

  // For stats.
  int num_rpc_;
  HttpRPC::Status status_;

  // for multi store
  MultiFileStore* multi_file_store_;

  DISALLOW_COPY_AND_ASSIGN(FileServiceHttpClient);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMA_FILE_HTTP_H_
