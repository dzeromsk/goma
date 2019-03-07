// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob/file_blob_downloader.h"

#include "glog/logging.h"

namespace devtools_goma {

FileBlobDownloader::FileBlobDownloader(
    std::unique_ptr<FileServiceBlobDownloader> file_service_downloader)
    : file_service_downloader_(std::move(file_service_downloader)) {}

bool FileBlobDownloader::Download(const ExecResult_Output& output,
                                  OutputFileInfo* info) {
  // TODO: Add more delegate downloaders.
  const auto blob_type = output.blob().blob_type();
  switch (blob_type) {
    case FileBlob::FILE:  // TODO: Add separate delegate for this.
    case FileBlob::FILE_META:
      return file_service_downloader_->Download(output, info);

    case FileBlob::FILE_CHUNK:
    case FileBlob::FILE_UNSPECIFIED:
      LOG(ERROR) << "Unable to handle blob type "
                 << FileBlob::BlobType_Name(blob_type);
      return false;
  }
}

}  // namespace devtools_goma
