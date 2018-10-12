// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_GOMA_FILE_H_
#define DEVTOOLS_GOMA_LIB_GOMA_FILE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "lib/file_reader.h"
#include "prototmp/goma_data.pb.h"
using std::string;

#ifdef _WIN32
# include <shlobj.h>
# include <strsafe.h>
# include "config_win.h"
#endif

namespace devtools_goma {

class FileBlob;
class StoreFileReq;
class StoreFileResp;
class LookupFileReq;
class LookupFileResp;
class ScopedFd;

class FileServiceClient {
 public:
  // Asynchronous support on old synchronous http rpc.
  // TODO: provide proto-service style async call.
  template<typename Req, typename Resp>
  class AsyncTask {
   public:
    AsyncTask() {}
    virtual ~AsyncTask() {}
    const Req& req() const { return req_; }
    Req* mutable_req() { return &req_; }
    const Resp& resp() const { return resp_; }
    Resp* mutable_resp() { return &resp_; }
    virtual void Run() = 0;
    virtual void Wait() = 0;

    virtual bool IsSuccess() const = 0;

   protected:
    Req req_;
    Resp resp_;

   private:
    DISALLOW_COPY_AND_ASSIGN(AsyncTask);
  };
  // TODO: provide Input too.
  // Output is an abstract interface of output from FileServiceClient.
  class Output {
   public:
    Output() {}
    virtual ~Output() {}
    // IsValid returns true if this output is valid to use.
    virtual bool IsValid() const = 0;
    // WriteAt writes content at offset in output.
    virtual bool WriteAt(off_t offset, const string& content) = 0;
    // Close closes the output.
    virtual bool Close() = 0;
    // ToString returns string representation of this output. e.g. filename.
    virtual string ToString() const = 0;
   private:
    DISALLOW_COPY_AND_ASSIGN(Output);
  };

  // FileOutput returns Output for filename.
  static std::unique_ptr<Output> FileOutput(const string& filename, int mode);
  // StringOutput returns Output into buf.
  // It doesn't take ownership of buf.
  // *buf will have output size when Close().
  // Note that, unlike sparse file in unix, it will not modify data in a hole,
  // if the hole exists. This class won't create any sparse file, so may not
  // need to worry about this.
  // If you care, pass empty buf (StringOutput will
  // allocate enough space), or zero-cleared preallocated buf.
  static std::unique_ptr<Output> StringOutput(const string& name, string* buf);

  FileServiceClient()
      : reader_factory_(FileReaderFactory::GetInstance()) {}
  virtual ~FileServiceClient() {}

  // Create |blob| for |filename|.
  // If failed to open |filename|, it will set FileBlob::FILE as blob_type
  // and set file_size=-1, which is considered as an invalid FileBlob.
  // If |store_large| is true and the file is large enough, it will also store
  // file chunks in file service.
  // Note that |blob| itself will not be stored in file service by this method,
  // so need to use StoreFileBlob() to store.
  // Returns true on success, false on error.
  bool CreateFileBlob(const string& filename, bool store_large, FileBlob* blob);

  // Store |blob| in file service.
  // Returns true on success, false on error.
  bool StoreFileBlob(const FileBlob& blob);

  // Store muliple |blob|s in file service.
  // Returns true on success, false on error.
  bool StoreFileBlobs(const std::vector<FileBlob*>& blobs);

  // Gets |blob| for |hash_key|.
  // Returns true on success, false on error.
  bool GetFileBlob(const string& hash_key, FileBlob* blob);

  // Gets |blobs| for |hash_keys|.
  // Returns true on success, false on error.
  // Even if it returns true, blobs may contain invalid FileBlob, which means
  // missing content for the corresponding hash_key.
  bool GetFileBlobs(const std::vector<string>& hash_keys,
                    std::vector<FileBlob*>* blobs);

  // Writes |blob| to |filename|.
  // convenient helper for OutputFileBlob().
  bool WriteFileBlob(const string& filename, int mode, const FileBlob& blob);

  // OutputFileBlob outputs blob into output.
  // It doesn't take ownership of output.
  // If the blob_type is FILE_META, it will also fetch file chunks in
  // file service.
  // Returns true on success, false on error.  output will be closed in
  // this method.
  bool OutputFileBlob(const FileBlob& blob, Output* output);

  // Checks |blob| is valid.
  static bool IsValidFileBlob(const FileBlob& blob);

  // Compute hash key of |blob|.
  static string ComputeHashKey(const FileBlob& blob);

  virtual std::unique_ptr<AsyncTask<StoreFileReq, StoreFileResp>>
  NewAsyncStoreFileTask() = 0;
  virtual std::unique_ptr<AsyncTask<LookupFileReq, LookupFileResp>>
  NewAsyncLookupFileTask() = 0;

  virtual bool StoreFile(const StoreFileReq* req, StoreFileResp* resp) = 0;
  virtual bool LookupFile(const LookupFileReq* req, LookupFileResp* resp) = 0;

 protected:
  FileReaderFactory* reader_factory_;
  std::unique_ptr<RequesterInfo> requester_info_;
  string trace_id_;

 private:
  bool FinishStoreFileTask(
      std::unique_ptr<AsyncTask<StoreFileReq, StoreFileResp>> task);

  // Note: off_t is 32-bit in Windows.  If need to handle files bigger than
  //       4GB, it needs to be changed to QWORD.
  bool CreateFileChunks(FileReader* fr,
                        off_t size, bool store, FileBlob* blob);
  bool ReadFileContent(FileReader* fr,
                       off_t offset, off_t size, FileBlob* blob);

  bool OutputLookupFileResp(const LookupFileReq& req,
                            const LookupFileResp& resp,
                            Output* output);
  bool FinishLookupFileTask(
      std::unique_ptr<AsyncTask<LookupFileReq, LookupFileResp>> task,
      Output* output);
  bool OutputFileChunks(const FileBlob& blob, Output* output);

  DISALLOW_COPY_AND_ASSIGN(FileServiceClient);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_GOMA_FILE_H_
