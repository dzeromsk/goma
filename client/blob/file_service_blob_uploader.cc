// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob/file_service_blob_uploader.h"

#include "glog/logging.h"
#include "goma_data_util.h"

namespace devtools_goma {

FileServiceBlobUploader::FileServiceBlobUploader(
    string filename,
    std::unique_ptr<FileServiceHttpClient> file_service)
    : BlobClient::Uploader(std::move(filename)),
      file_service_(std::move(file_service)),
      blob_(absl::make_unique<FileBlob>()) {}

bool FileServiceBlobUploader::ComputeKey() {
  bool success = file_service_->CreateFileBlob(filename_, false, blob_.get());
  if (success && IsValidFileBlob(*blob_)) {
    hash_key_ = ComputeFileBlobHashKey(*blob_);
    return true;
  }
  return success;
}

bool FileServiceBlobUploader::Upload() {
  blob_->Clear();
  bool success = file_service_->CreateFileBlob(filename_, true, blob_.get());
  if (success && IsValidFileBlob(*blob_)) {
    hash_key_ = ComputeFileBlobHashKey(*blob_);
    need_blob_ = true;
    return true;
  }
  return false;
}

bool FileServiceBlobUploader::Embed() {
  if (!hash_key_.empty()) {
    // already loaded into blob_.
    need_blob_ = true;
    return true;
  }
  blob_->Clear();
  bool success = file_service_->CreateFileBlob(filename_, false, blob_.get());
  if (success && IsValidFileBlob(*blob_)) {
    hash_key_ = ComputeFileBlobHashKey(*blob_);
    need_blob_ = true;
    return true;
  }
  return false;
}

bool FileServiceBlobUploader::GetInput(ExecReq_Input* input) const {
  // |input| should have filename.
  // |this->filename_| is abspath, so should not be used here.
  CHECK(input->has_filename());

  input->set_hash_key(hash_key_);
  if (!need_blob_) {
    return true;
  }
  *input->mutable_content() = *blob_;
  return IsValidFileBlob(input->content());
}

bool FileServiceBlobUploader::Store() const {
  if (!blob_) {
    return false;
  }
  if (!IsValidFileBlob(*blob_)) {
    return false;
  }
  return file_service_->StoreFileBlob(*blob_);
}

}  // namespace devtools_goma
