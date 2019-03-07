// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_GOMA_BLOB_H_
#define DEVTOOLS_GOMA_CLIENT_GOMA_BLOB_H_

#include <memory>
#include <string>

#include "goma_file_http.h"
#include "http.h"

namespace devtools_goma {

class ExecReq_Input;
class ExecResult_Output;
class FileServiceClient;
class RequesterInfo;

// BlobClient uploads/downloads file blob between client and server.
class BlobClient {
 public:
  // Uploader uploads file blob from client to server.
  class Uploader {
   public:
    virtual ~Uploader() = default;

    Uploader(Uploader&&) = delete;
    Uploader(const Uploader&) = delete;
    Uploader& operator=(const Uploader&) = delete;
    Uploader& operator=(Uploader&&) = delete;

    // Computes hash key of the file.
    // Input data will not have any content. hash only.
    virtual bool ComputeKey() = 0;

    // Uploads file blob to server.
    virtual bool Upload() = 0;

    // Embeds file blob in input.
    virtual bool Embed() = 0;

    // Following methods are valid only after one of above 3 methods call.
    const std::string& hash_key() const { return hash_key_; }
    virtual const HttpClient::Status& http_status() const = 0;

    // Fills in input.
    virtual bool GetInput(ExecReq_Input* input) const = 0;

    // Stores remaining file blob and confirms file blob is uploaded
    // to the server after Uploads.
    // It is used to send file contents without Exec request.
    virtual bool Store() const = 0;

   protected:
    explicit Uploader(std::string filename);

    const std::string filename_;
    std::string hash_key_;
  };
  // Downloader downloads file blob from server to client.
  class Downloader {
   public:
    // Describes the output of a file download operation. The file data can be
    // downloaded to either a file (|tmp_file_name| != "") or stored directly in
    // |content|.
    struct OutputFileInfo {
      // actual output filename.
      string filename;
      // file mode/permission.
      int mode = 0666;

      size_t size = 0;

      // tmp_filename is filename written by OutputFileTask.
      // tmp_filename may be the same as output filename (when !need_rename), or
      // rename it to real output filename in CommitOutput().
      // if tmp file was not written in OutputFileTask, because it holds content
      // in content field, tmp_filename will be "".
      string tmp_filename;

      // hash_key is hash of output filename. It will be stored in file hash
      // cache once output file is committed.
      // TODO: fix this to support cas digest.
      string hash_key;

      // content is output content.
      // it is used to hold output content in memory while output file task.
      // it will be used iff tmp_filename == "".
      string content;
    };

    virtual ~Downloader() = default;

    Downloader(Downloader&&) = delete;
    Downloader(const Downloader&) = delete;
    Downloader& operator=(const Downloader&) = delete;
    Downloader& operator=(Downloader&&) = delete;

    // Downloads file content specified by |output| into the destination sink
    // defined by |sink|.
    virtual bool Download(const ExecResult_Output& output,
                          OutputFileInfo* sink) = 0;

    virtual int num_rpc() const = 0;
    virtual const HttpClient::Status& http_status() const = 0;

   protected:
    Downloader() = default;
  };

  virtual ~BlobClient() = default;

  BlobClient(BlobClient&&) = delete;
  BlobClient(const BlobClient&) = delete;
  BlobClient& operator=(const BlobClient&) = delete;
  BlobClient& operator=(BlobClient&&) = delete;

  // NewUploader creates new uploader for the filename.
  virtual std::unique_ptr<Uploader> NewUploader(
      std::string filename,
      const RequesterInfo& requester_info,
      std::string trace_id) = 0;

  // NewDownloader creates new downloader.
  virtual std::unique_ptr<Downloader> NewDownloader(
      const RequesterInfo& requester_info,
      std::string trace_id) = 0;

 protected:
  BlobClient() = default;
};

// FileBlobClient is a BlobClient that handles FileBlobs.
class FileBlobClient : public BlobClient {
 public:
  explicit FileBlobClient(
      std::unique_ptr<FileServiceHttpClient> file_service_client);
  ~FileBlobClient() override = default;

  std::unique_ptr<BlobClient::Uploader> NewUploader(
      std::string filename,
      const RequesterInfo& requester_info,
      std::string trace_id) override;

  std::unique_ptr<BlobClient::Downloader> NewDownloader(
      const RequesterInfo& requester_info,
      std::string trace_id) override;

 private:
  // For handling FileBlobs in FileService over HTTP.
  std::unique_ptr<FileServiceHttpClient> file_service_client_;

  // TODO: Add BlobClients for other types of FileBlob handling.
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMA_BLOB_H_
