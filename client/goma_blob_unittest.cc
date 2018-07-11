// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "goma_blob.h"

#include <memory>
#include <string>

#include "absl/memory/memory.h"
#include "basictypes.h"
#include "compiler_specific.h"
#include "goma_file_http.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "gtest/gtest.h"

namespace devtools_goma {

// example of BlobClient.
class ExampleBlobClient : public BlobClient {
 public:
  class Uploader : public BlobClient::Uploader {
   public:
    Uploader(string filename, ExampleBlobClient* client)
        : BlobClient::Uploader(std::move(filename)),
          client_(client),
          blob_(absl::make_unique<FileBlob>()) {
    }
    ~Uploader() override = default;

    bool ComputeKey() override {
      hash_key_ = "dummy-hash-key";
      return true;
    }
    bool Upload() override {
      hash_key_ = "dummy-hash-key";
      client_->uploaded_ = true;
      need_blob_ = true;
      return true;
    }
    bool Embed() override {
      hash_key_ = "dummy-hash-key";
      need_blob_ = true;
      return true;
    }

    const HttpClient::Status& http_status() const override {
      return http_status_;
    }

    bool GetInput(ExecReq_Input* input) const override {
      input->set_filename(filename_);
      input->set_hash_key(hash_key_);
      if (!need_blob_) {
        return true;
      }
      *input->mutable_content() = *blob_;
      return true;
    }

   private:
    ExampleBlobClient* client_;
    std::unique_ptr<FileBlob> blob_;
    bool need_blob_ = false;
    HttpClient::Status http_status_;
  };

  ExampleBlobClient() = default;
  virtual ~ExampleBlobClient() override = default;

  std::unique_ptr<BlobClient::Uploader> NewUploader(
      std::string filename,
      const RequesterInfo& requester_info,
      std::string trace_id) override {
    return absl::make_unique<Uploader>(std::move(filename), this);
  }

  bool uploaded() const { return uploaded_; }

 private:
  bool uploaded_ = false;
};

TEST(BlobClient, ExampleComputeKey) {
  std::unique_ptr<ExampleBlobClient> blob_client =
      absl::make_unique<ExampleBlobClient>();

  RequesterInfo requester_info;
  std::unique_ptr<BlobClient::Uploader> uploader =
      blob_client->NewUploader(
          "/path/to/filename",
          requester_info, "trace_id");

  EXPECT_TRUE(uploader->ComputeKey());
  EXPECT_EQ("dummy-hash-key", uploader->hash_key());
  ExecReq_Input input;
  EXPECT_TRUE(uploader->GetInput(&input));
  EXPECT_EQ(uploader->hash_key(), input.hash_key());
  EXPECT_EQ("/path/to/filename", input.filename());
  EXPECT_FALSE(input.has_content());
  EXPECT_FALSE(blob_client->uploaded());
}

TEST(BlobClient, ExampleUpload) {
  std::unique_ptr<ExampleBlobClient> blob_client =
      absl::make_unique<ExampleBlobClient>();

  RequesterInfo requester_info;
  std::unique_ptr<BlobClient::Uploader> uploader =
      blob_client->NewUploader(
          "/path/to/filename",
          requester_info, "trace_id");

  EXPECT_TRUE(uploader->Upload());
  EXPECT_EQ("dummy-hash-key", uploader->hash_key());
  ExecReq_Input input;
  EXPECT_TRUE(uploader->GetInput(&input));
  EXPECT_EQ(uploader->hash_key(), input.hash_key());
  EXPECT_EQ("/path/to/filename", input.filename());
  EXPECT_TRUE(input.has_content());
  EXPECT_TRUE(blob_client->uploaded());
}

TEST(BlobClient, ExampleEmbed) {
  std::unique_ptr<ExampleBlobClient> blob_client =
      absl::make_unique<ExampleBlobClient>();

  RequesterInfo requester_info;
  std::unique_ptr<BlobClient::Uploader> uploader =
      blob_client->NewUploader(
          "/path/to/filename",
          requester_info, "trace_id");

  EXPECT_TRUE(uploader->Embed());
  EXPECT_EQ("dummy-hash-key", uploader->hash_key());
  ExecReq_Input input;
  EXPECT_TRUE(uploader->GetInput(&input));
  EXPECT_EQ(uploader->hash_key(), input.hash_key());
  EXPECT_EQ("/path/to/filename", input.filename());
  EXPECT_TRUE(input.has_content());
  EXPECT_FALSE(blob_client->uploaded());
}

}  // namespace devtools_goma
