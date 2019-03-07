// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_BLOB_FILE_SERVICE_BLOB_DOWNLOADER_H_
#define DEVTOOLS_GOMA_CLIENT_BLOB_FILE_SERVICE_BLOB_DOWNLOADER_H_

#include <memory>

#include "goma_blob.h"

namespace devtools_goma {

class FileServiceHttpClient;

class FileServiceBlobDownloader : public BlobClient::Downloader {
 public:
  explicit FileServiceBlobDownloader(
      std::unique_ptr<FileServiceHttpClient> file_service);
  ~FileServiceBlobDownloader() override = default;

  bool Download(const ExecResult_Output& output, OutputFileInfo* info) override;

  int num_rpc() const override { return file_service_->num_rpc(); }

  const HttpClient::Status& http_status() const override {
    return file_service_->http_rpc_status();
  }

 private:
  std::unique_ptr<FileServiceHttpClient> file_service_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_BLOB_FILE_SERVICE_BLOB_DOWNLOADER_H_
