// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "goma_blob.h"

#include "absl/memory/memory.h"
#include "basictypes.h"
#include "compiler_specific.h"
#include "glog/logging.h"
#include "goma_file.h"
#include "goma_file_http.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

BlobClient::Uploader::Uploader(string filename)
    : filename_(std::move(filename)) {
}

class FileServiceBlobUploader : public BlobClient::Uploader {
 public:
  FileServiceBlobUploader(
      string filename,
      std::unique_ptr<FileServiceHttpClient> file_service)
      : BlobClient::Uploader(std::move(filename)),
        file_service_(std::move(file_service)),
        blob_(absl::make_unique<FileBlob>()) {
  }
  ~FileServiceBlobUploader() override = default;

  bool ComputeKey() override {
    bool success = file_service_->CreateFileBlob(
        filename_, false, blob_.get());
    if (success && FileServiceClient::IsValidFileBlob(*blob_)) {
      hash_key_ = FileServiceClient::ComputeHashKey(*blob_);
      return true;
    }
    return success;
  }

  bool Upload() override {
    blob_->Clear();
    bool success = file_service_->CreateFileBlob(
        filename_, true, blob_.get());
    if (success && FileServiceClient::IsValidFileBlob(*blob_)) {
      hash_key_ = FileServiceClient::ComputeHashKey(*blob_);
      need_blob_ = true;
      return true;
    }
    return false;
  }

  bool Embed() override {
    if (!hash_key_.empty()) {
      // already loaded into blob_.
      need_blob_ = true;
      return true;
    }
    blob_->Clear();
    bool success = file_service_->CreateFileBlob(
        filename_, false, blob_.get());
    if (success && FileServiceClient::IsValidFileBlob(*blob_)) {
      hash_key_ = FileServiceClient::ComputeHashKey(*blob_);
      need_blob_ = true;
      return true;
    }
    return false;
  }

  const HttpClient::Status& http_status() const override {
    return file_service_->http_rpc_status();
  }

  bool GetInput(ExecReq_Input* input) const override {
    // |input| should have filename.
    // |this->filename_| is abspath, so should not be used here.
    CHECK(input->has_filename());

    input->set_hash_key(hash_key_);
    if (!need_blob_) {
      return true;
    }
    *input->mutable_content() = *blob_;
    return FileServiceClient::IsValidFileBlob(input->content());
  }

 private:
  std::unique_ptr<FileServiceHttpClient> file_service_;
  std::unique_ptr<FileBlob> blob_;
  bool need_blob_ = false;
};

std::unique_ptr<BlobClient::Uploader>
FileServiceBlobClient::NewUploader(
    string filename,
    const RequesterInfo& requester_info,
    string trace_id) {
  return absl::make_unique<FileServiceBlobUploader>(
      std::move(filename),
      file_service_->WithRequesterInfoAndTraceId(
          requester_info,
          std::move(trace_id)));
}

}  // namespace devtools_goma
