// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_BLOB_FILE_SERVICE_BLOB_UPLOADER_H_
#define DEVTOOLS_GOMA_CLIENT_BLOB_FILE_SERVICE_BLOB_UPLOADER_H_

#include <memory>

#include "goma_blob.h"

namespace devtools_goma {

class FileBlob;
class FileServiceHttpClient;

class FileServiceBlobUploader : public BlobClient::Uploader {
 public:
  FileServiceBlobUploader(string filename,
                          std::unique_ptr<FileServiceHttpClient> file_service);
  ~FileServiceBlobUploader() override = default;

  bool ComputeKey() override;

  bool Upload() override;

  bool Embed() override;

  const HttpClient::Status& http_status() const override {
    return file_service_->http_rpc_status();
  }

  bool GetInput(ExecReq_Input* input) const override;

  bool Store() const override;

 private:
  std::unique_ptr<FileServiceHttpClient> file_service_;
  std::unique_ptr<FileBlob> blob_;
  bool need_blob_ = false;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_BLOB_FILE_SERVICE_BLOB_UPLOADER_H_
