// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_BLOB_FILE_BLOB_DOWNLOADER_H_
#define DEVTOOLS_GOMA_CLIENT_BLOB_FILE_BLOB_DOWNLOADER_H_

#include <memory>

#include "blob/file_service_blob_downloader.h"
#include "goma_blob.h"

namespace devtools_goma {

// FileBlobDownloader handles downloads of various types of FileBlobs. Different
// types of FileBlobs are handled by different delegate classes.
class FileBlobDownloader : public BlobClient::Downloader {
 public:
  FileBlobDownloader(
      std::unique_ptr<FileServiceBlobDownloader> file_service_downloader);
  ~FileBlobDownloader() override = default;

  bool Download(const ExecResult_Output& output, OutputFileInfo* info) override;

  // TODO: Handle stats for cases that don't use
  // |file_service_downloader_|.
  int num_rpc() const override { return file_service_downloader_->num_rpc(); }

  const HttpClient::Status& http_status() const override {
    return file_service_downloader_->http_status();
  }

 private:
  std::unique_ptr<FileServiceBlobDownloader> file_service_downloader_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_BLOB_FILE_BLOB_DOWNLOADER_H_
