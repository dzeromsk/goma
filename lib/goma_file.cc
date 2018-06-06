// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "goma_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <memory>
#include <stack>
#include <utility>


#include "compiler_specific.h"
#include "file.h"
#include "glog/logging.h"
#include "goma_hash.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()
#include "scoped_fd.h"
using std::string;

namespace {

const size_t kLargeFileThreshold = 2 * 1024 * 1024UL;  // 2MB
const off_t kFileChunkSize = 2 * 1024 * 1024L;

const int kNumChunksInStreamRequest = 5;

bool CreateDirectoryForFile(const string& filename) {
#ifndef _WIN32
  std::stack<string> ancestors;
  size_t last_slash = filename.rfind('/');
  while (last_slash != string::npos) {
    const string& dirname = filename.substr(0, last_slash);
    int result = mkdir(dirname.c_str(), 0777);
    if (result == 0) {
      VLOG(1) << "created " << dirname << " to store " << filename;
      break;
    }
    if (errno == EEXIST) {
      // Other threads created this directory.
      break;
    }
    if (errno != ENOENT) {
      PLOG(INFO) << "failed to create directory: " << dirname;
      return false;
    }
    ancestors.push(dirname);
    last_slash = filename.rfind('/', last_slash - 1);
  }

  while (!ancestors.empty()) {
    const string& dirname = ancestors.top();
    int result = mkdir(dirname.c_str(), 0777);
    if (result < 0 && errno != EEXIST) {
      PLOG(INFO) << "failed to create directory: " << dirname;
      return false;
    }
    VLOG(1) << "created " << dirname << " to store " << filename;
    ancestors.pop();
  }
  return true;
#else
  size_t last_slash = filename.rfind('\\');
  const string& dirname = filename.substr(0, last_slash);
  int result = SHCreateDirectoryExA(nullptr, dirname.c_str(), nullptr);
  if (result == ERROR_SUCCESS) {
    VLOG(1) << "created " << dirname;
  } else if (result == ERROR_FILE_EXISTS) {
    // Other threads created this directory.
  } else {
    PLOG(INFO) << "failed to create directory: " << dirname;
    return false;
  }
  return true;
#endif
}

class FileOutputImpl : public devtools_goma::FileServiceClient::Output {
 public:
  FileOutputImpl(const string& filename, int mode)
      : filename_(filename),
        fd_(devtools_goma::ScopedFd::Create(filename, mode)),
        error_(false) {
    bool not_found_error = false;
#ifndef _WIN32
    not_found_error = !fd_.valid() && errno == ENOENT;
#else
    not_found_error = !fd_.valid() && GetLastError() == ERROR_PATH_NOT_FOUND;
#endif
    if (!not_found_error) {
      return;
    }
    if (!CreateDirectoryForFile(filename)) {
      PLOG(INFO) << "failed to create directory for " << filename;
      // other threads/process may create the same dir, so next
      // open might succeed.
    }
    fd_.reset(devtools_goma::ScopedFd::Create(filename, mode));
    if (!fd_.valid()) {
      PLOG(ERROR) << "open failed:" << filename;
    }
  }
  ~FileOutputImpl() override {
    if (error_) {
      VLOG(1) << "Write failed. delete " << filename_;
      remove(filename_.c_str());
    }
  }

  bool IsValid() const override {
    return fd_.valid();
  }
  bool WriteAt(off_t offset, const string& content) override {
    off_t pos = fd_.Seek(offset, devtools_goma::ScopedFd::SeekAbsolute);
    if (pos < 0 || pos != offset) {
      PLOG(ERROR) << "seek failed? " << filename_
                  << " pos=" << pos << " offset=" << offset;
      error_ = true;
      return false;
    }
    size_t written = 0;
    while (written < content.size()) {
      int n = fd_.Write(content.data() + written, content.size() - written);
      if (n < 0) {
        PLOG(WARNING) << "write failed " << filename_;
        error_ = true;
        return false;
      }
      written += n;
    }
    return true;
  }

  bool Close() override {
    bool r = fd_.Close();
    if (!r) {
      error_ = true;
    }
    return r;
  }

  string ToString() const override {
    return filename_;
  }

 private:
  const string filename_;
  devtools_goma::ScopedFd fd_;
  bool error_;
  DISALLOW_COPY_AND_ASSIGN(FileOutputImpl);
};

class StringOutputImpl : public devtools_goma::FileServiceClient::Output {
 public:
  StringOutputImpl(string name, string* buf)
      : name_(std::move(name)), buf_(buf), size_(0UL) {}
  ~StringOutputImpl() override {
  }

  bool IsValid() const override { return buf_ != nullptr; }
  bool WriteAt(off_t offset, const string& content) override {
    if (buf_->size() < offset + content.size()) {
      buf_->resize(offset + content.size());
    }
    if (content.size() > 0) {
      memcpy(&(buf_->at(offset)), content.data(), content.size());
    }
    if (size_ < offset + content.size()) {
      size_ = offset + content.size();
    }
    return true;
  }

  bool Close() override {
    buf_->resize(size_);
    return true;
  }
  string ToString() const override { return name_; }

 private:
  const string name_;
  string* buf_;
  size_t size_;
  DISALLOW_COPY_AND_ASSIGN(StringOutputImpl);
};

}  // anonymous namespace

namespace devtools_goma {

static string GetHashKeyInLookupFileReq(const LookupFileReq& req, int i) {
  CHECK_GE(i, 0);
  if (i < req.hash_key_size())
    return req.hash_key(i);
  return "(out of range)";
}

/* static */
std::unique_ptr<FileServiceClient::Output> FileServiceClient::FileOutput(
    const string& filename, int mode) {
  return std::unique_ptr<FileServiceClient::Output>(
      new FileOutputImpl(filename, mode));
}

/* static */
std::unique_ptr<FileServiceClient::Output> FileServiceClient::StringOutput(
    const string& name, string* buf) {
  return std::unique_ptr<FileServiceClient::Output>(
      new StringOutputImpl(name, buf));
}

bool FileServiceClient::CreateFileBlob(
    const string& filename, bool store_large, FileBlob* blob) {
  VLOG(1) << "CreateFileBlob " << filename;
  blob->set_blob_type(FileBlob::FILE);
  blob->set_file_size(-1);
  bool ok = false;

  std::unique_ptr<FileReader> reader(reader_factory_->NewFileReader(filename));
  size_t file_size = 0;
  if (!reader->valid()) {
    LOG(WARNING) << "open failed: " << filename;
    return false;
  }
  if (!reader->GetFileSize(&file_size)) {
    LOG(WARNING) << "stat failed: " << filename;
    return false;
  }
  blob->set_file_size(file_size);
  VLOG(1) << filename << " size=" << file_size;
  if (file_size > kLargeFileThreshold) {
    ok = CreateFileChunks(reader.get(), file_size, store_large, blob);
  } else {
    ok = ReadFileContent(reader.get(), 0, file_size, blob);
  }

  if (ok) {
    VLOG(1) << "CreateFileBlob " << filename << " ok";
  } else {
    LOG(WARNING) << "CreateFileBlob " << filename << " failed";
  }
  return ok;
}

bool FileServiceClient::StoreFileBlob(const FileBlob& blob) {
  VLOG(1) << "StoreFileBlob";
  if (blob.blob_type() == FileBlob::FILE && blob.file_size() < 0) {
    VLOG(1) << "Invalid FileBlob";
    return false;
  }

  FileBlob* req_blob = const_cast<FileBlob*>(&blob);
  StoreFileReq req;
  StoreFileResp resp;
  req.add_blob()->Swap(req_blob);
  if (requester_info_ != nullptr) {
    *req.mutable_requester_info() = *requester_info_;
  }
  bool ok = StoreFile(&req, &resp);
  req_blob->Swap(req.mutable_blob(0));
  VLOG(1) << "StoreFileBlob " << (ok ? "ok" : "failed");
  return ok;
}

bool FileServiceClient::StoreFileBlobs(const std::vector<FileBlob*>& blobs) {
  VLOG(1) << "StoreFileBlobs num=" << blobs.size();
  StoreFileReq req;
  StoreFileResp resp;
  for (size_t i = 0; i < blobs.size(); ++i) {
    if (blobs[i]->blob_type() == FileBlob::FILE && blobs[i]->file_size() < 0) {
      LOG(WARNING) << "blobs[" << i << "] is invalid FileBlob";
      return false;
    }
    req.add_blob()->Swap(blobs[i]);
  }
  if (requester_info_ != nullptr) {
    *req.mutable_requester_info() = *requester_info_;
  }
  bool ok = StoreFile(&req, &resp);
  for (size_t i = 0; i < blobs.size(); ++i) {
    blobs[i]->Swap(req.mutable_blob(i));
  }
  return ok;
}

bool FileServiceClient::GetFileBlob(const string& hash_key, FileBlob* blob) {
  VLOG(1) << "GetFileBlob " << hash_key;
  LookupFileReq req;
  LookupFileResp resp;
  req.add_hash_key(hash_key);
  if (requester_info_ != nullptr) {
    *req.mutable_requester_info() = *requester_info_;
  }
  if (!LookupFile(&req, &resp)) {
    VLOG(1) << "LookupFile failed";
    return false;
  }
  if (resp.blob_size() < 1) {
    LOG(WARNING) << "no resp.blob()";
    return false;
  }
  blob->Swap(resp.mutable_blob(0));
  return true;
}

bool FileServiceClient::GetFileBlobs(const std::vector<string>& hash_keys,
                                     std::vector<FileBlob*>* blobs) {
  VLOG(1) << "GetFileBlobs num=" << hash_keys.size();
  LookupFileReq req;
  LookupFileResp resp;
  for (const auto& key : hash_keys) {
    req.add_hash_key(key);
  }
  if (requester_info_ != nullptr) {
    *req.mutable_requester_info() = *requester_info_;
  }
  if (!LookupFile(&req, &resp)) {
    VLOG(1) << "LookupFile failed";
    return false;
  }
  DCHECK_EQ(hash_keys.size(), static_cast<unsigned int>(resp.blob_size()));
  for (int i = 0; i < resp.blob_size(); ++i) {
    FileBlob* blob = new FileBlob;
    blob->Swap(resp.mutable_blob(i));
    blobs->push_back(blob);
  }
  return true;
}

bool FileServiceClient::WriteFileBlob(const string& filename,
                                      int mode,
                                      const FileBlob& blob) {
  VLOG(1) << "WriteFileBlob " << filename;
  std::unique_ptr<Output> output = FileOutput(filename, mode);
  bool r = OutputFileBlob(blob, output.get());
  return r;
}

bool FileServiceClient::OutputFileBlob(const FileBlob& blob, Output* output) {
  if (!output->IsValid()) {
    LOG(ERROR) << "invalid output:" << output->ToString();
    return false;
  }
  bool ret = false;
  switch (blob.blob_type()) {
    case FileBlob::FILE:
      if (blob.file_size() >= 0) {
        ret = output->WriteAt(0, blob.content());
      } else {
        LOG(ERROR) << "Invalid FileBlob";
      }
      break;

    case FileBlob::FILE_META:
      ret = OutputFileChunks(blob, output);
      break;

    case FileBlob::FILE_CHUNK:
      LOG(ERROR) << "Can't write FILE_CHUNK";
      break;

    default:
      LOG(ERROR) << "Unknown blob_type:" << blob.blob_type();
      break;
  }
  if (!output->Close()) {
    PLOG(ERROR) << "Write close failed? " << output->ToString();
    ret = false;
  }
  return ret;
}

bool FileServiceClient::FinishStoreFileTask(
    std::unique_ptr<AsyncTask<StoreFileReq, StoreFileResp>> task) {
  if (!task)
    return true;
  VLOG(1) << "Wait StoreFileTask";
  task->Wait();
  VLOG(1) << "Finish StoreFileTask";
  if (!task->IsSuccess()) {
    LOG(WARNING) << "Finish StoreFileTask failed.";
    return false;
  }
  int num_failed = 0;
  for (int i = 0; i < task->resp().hash_key_size(); ++i) {
    if (task->resp().hash_key(i).empty()) {
      VLOG(1) << "No response at " << i;
      num_failed++;
    }
  }
  if (num_failed > 0) {
    LOG(WARNING) << "StoreFileTask failed " << num_failed << " chunks";
    return false;
  }
  return true;
}

bool FileServiceClient::CreateFileChunks(
    FileReader* fr, off_t size, bool store, FileBlob* blob) {
  VLOG(1) << "CreateFileChunks size=" << size;
  blob->set_blob_type(FileBlob::FILE_META);

  std::unique_ptr<AsyncTask<StoreFileReq, StoreFileResp> > task(
      NewAsyncStoreFileTask());
  if (store && task.get()) {
    // Streaming available.
    VLOG(1) << "Streaming mode";
    if (requester_info_ != nullptr) {
      *task->mutable_req()->mutable_requester_info() = *requester_info_;
    }
    std::unique_ptr<AsyncTask<StoreFileReq, StoreFileResp> > in_flight_task;
    for (off_t offset = 0; offset < size; offset += kFileChunkSize) {
      FileBlob* chunk = task->mutable_req()->add_blob();
      int chunk_size = std::min(kFileChunkSize, size - offset);
      if (!ReadFileContent(fr, offset, chunk_size, chunk)) {
        LOG(WARNING) << "ReadFile failed."
                     << " offset=" << offset << " chunk_size=" << chunk_size;
        return false;
      }
      chunk->set_blob_type(FileBlob::FILE_CHUNK);
      chunk->set_offset(offset);
      chunk->set_file_size(chunk_size);
      string hash_key = ComputeHashKey(*chunk);
      LOG(INFO) << "chunk hash_key:" << hash_key;
      blob->add_hash_key(hash_key);
      if (task->req().blob_size() >= kNumChunksInStreamRequest) {
        if (!FinishStoreFileTask(std::move(in_flight_task)))
          return false;
        task->Run();
        in_flight_task = std::move(task);
        task = NewAsyncStoreFileTask();
        if (requester_info_ != nullptr) {
          *task->mutable_req()->mutable_requester_info() = *requester_info_;
        }
      }
    }
    VLOG(1) << "ReadFile done";
    if (task->req().blob_size() > 0)
      task->Run();
    else
      task.reset(nullptr);
    if (!FinishStoreFileTask(std::move(in_flight_task))) {
      FinishStoreFileTask(std::move(task));
      return false;
    }
    return FinishStoreFileTask(std::move(task));
  }

  for (off_t offset = 0; offset < size; offset += kFileChunkSize) {
    StoreFileReq req;
    StoreFileResp resp;
    if (requester_info_ != nullptr) {
      *req.mutable_requester_info() = *requester_info_;
    }
    FileBlob* chunk = req.add_blob();
    int chunk_size = std::min(kFileChunkSize, size - offset);
    if (!ReadFileContent(fr, offset, chunk_size, chunk)) {
      LOG(WARNING) << "ReadFile failed."
                   << " offset=" << offset << " chunk_size=" << chunk_size;
      return false;
    }
    chunk->set_blob_type(FileBlob::FILE_CHUNK);
    chunk->set_offset(offset);
    chunk->set_file_size(chunk_size);
    string hash_key = ComputeHashKey(*chunk);
    VLOG(1) << "chunk hash_key:" << hash_key;
    blob->add_hash_key(hash_key);
    if (store) {
      if (!StoreFile(&req, &resp)) {
        LOG(WARNING) << "StoreFile failed";
        return false;
      }
      if (resp.hash_key(0) != hash_key) {
        LOG(WARNING) << "Wrong hash_key:" << resp.hash_key(0)
                     << "!=" << hash_key;
        return false;
      }
    }
  }
  return true;
}

bool FileServiceClient::ReadFileContent(FileReader* fr,
                                        off_t offset, off_t chunk_size,
                                        FileBlob* blob) {
  VLOG(1) << "ReadFileContent"
          << " offset=" << offset << " chunk_size=" << chunk_size;
  string* buf = blob->mutable_content();
  buf->resize(chunk_size);
  if (offset > 0) {
    blob->set_blob_type(FileBlob::FILE_CHUNK);
    blob->set_offset(offset);
  } else {
    blob->set_blob_type(FileBlob::FILE);
  }
  if (fr->Seek(offset, ScopedFd::SeekAbsolute) != offset) {
    PLOG(WARNING) << "Seek failed " << offset;
    blob->clear_content();
    return false;
  }
  off_t nread = 0;
  while (nread < chunk_size) {
    int n = fr->Read(&((*buf)[nread]), chunk_size - nread);
    if (n < 0) {
      PLOG(WARNING) << "read failed.";
      blob->clear_content();
      return false;
    }
    nread += n;
  }
  return true;
}

bool FileServiceClient::OutputLookupFileResp(
    const LookupFileReq& req,
    const LookupFileResp& resp,
    Output* output) {
  for (int i = 0; i < resp.blob_size(); ++i) {
    const FileBlob& blob = resp.blob(i);
    if (!IsValidFileBlob(blob)) {
      LOG(WARNING) << "no FILE_CHUNK available at " << i << ": "
                   << GetHashKeyInLookupFileReq(req, i)
                   << " blob=" << blob.DebugString();
      return false;
    }
    if (blob.blob_type() == FileBlob::FILE_META) {
      LOG(WARNING) << "Wrong blob_type at " << i << ": "
                   << GetHashKeyInLookupFileReq(req, i)
                   << " blob=" << blob.DebugString();
      return false;
    }
    if (!output->WriteAt(static_cast<off_t>(blob.offset()), blob.content())) {
      LOG(WARNING) << "WriteFileContent failed.";
      return false;
    }
  }
  return true;
}

bool FileServiceClient::FinishLookupFileTask(
    std::unique_ptr<AsyncTask<LookupFileReq, LookupFileResp>> task,
    Output* output) {
  if (!task)
    return true;
  VLOG(1) << "Wait LookupFileTask";
  task->Wait();
  VLOG(1) << "Finish LookupFileTask";
  if (!task->IsSuccess()) {
    LOG(WARNING) << "Finish LookupFileTask failed.";
    return false;
  }
  return OutputLookupFileResp(task->req(), task->resp(), output);
}

bool FileServiceClient::OutputFileChunks(const FileBlob& blob, Output* output) {
  VLOG(1) << "OutputFileChunks";
  if (blob.blob_type() != FileBlob::FILE_META) {
    LOG(WARNING) << "wrong blob_type " << blob.blob_type();
    return false;
  }

  std::unique_ptr<AsyncTask<LookupFileReq, LookupFileResp> > task(
      NewAsyncLookupFileTask());
  if (task.get()) {
    // Streaming available.
    VLOG(1) << "Streaming mode";
    if (requester_info_ != nullptr) {
      *task->mutable_req()->mutable_requester_info() = *requester_info_;
    }
    std::unique_ptr<AsyncTask<LookupFileReq, LookupFileResp> > in_flight_task;
    for (const auto& key : blob.hash_key()) {
      task->mutable_req()->add_hash_key(key);
      VLOG(1) << "chunk hash_key:" << key;
      if (task->req().hash_key_size() >= kNumChunksInStreamRequest) {
        if (!FinishLookupFileTask(std::move(in_flight_task), output))
          return false;
        task->Run();
        in_flight_task = std::move(task);
        task = NewAsyncLookupFileTask();
        if (requester_info_ != nullptr) {
          *task->mutable_req()->mutable_requester_info() = *requester_info_;
        }
      }
    }
    VLOG(1) << "LookupFile done";
    if (task->req().hash_key_size() > 0)
      task->Run();
    else
      task.reset(nullptr);
    if (!FinishLookupFileTask(std::move(in_flight_task), output)) {
      FinishLookupFileTask(std::move(task), output);
      return false;
    }

    return FinishLookupFileTask(std::move(task), output);
  }

  for (const auto& key : blob.hash_key()) {
    LookupFileReq req;
    LookupFileResp resp;
    req.add_hash_key(key);
    if (requester_info_ != nullptr) {
      *req.mutable_requester_info() = *requester_info_;
    }
    VLOG(1) << "chunk hash_key:" << key;
    if (!LookupFile(&req, &resp)) {
      LOG(WARNING) << "Lookup failed.";
      return false;
    }
    if (resp.blob_size() < 1) {
      LOG(WARNING) << "no resp.blob()";
      return false;
    }
    if (!OutputLookupFileResp(req, resp, output)) {
      LOG(WARNING) << "Write response failed";
      return false;
    }
  }
  return true;
}

/* static */
bool FileServiceClient::IsValidFileBlob(const FileBlob& blob) {
  if (!blob.has_file_size())
    return false;
  if (blob.file_size() < 0)
    return false;

  switch (blob.blob_type()) {
    case FileBlob::FILE:
      if (blob.has_offset())
        return false;
      if (!blob.has_content())
        return false;
      if (blob.hash_key_size() > 0)
        return false;
      return true;

    case FileBlob::FILE_META:
      if (blob.has_offset())
        return false;
      if (blob.has_content())
        return false;
      if (blob.hash_key_size() <= 1)
        return false;
      return true;

    case FileBlob::FILE_CHUNK:
      if (!blob.has_offset())
        return false;
      if (!blob.has_content())
        return false;
      if (blob.hash_key_size() > 0)
        return false;
      return true;

    default:
      return false;
  }
}

/* static */
string FileServiceClient::ComputeHashKey(const FileBlob& blob) {
  string s;
  blob.SerializeToString(&s);
  string md_str;
  ComputeDataHashKey(s, &md_str);
  return md_str;
}

}  // namespace devtools_goma
