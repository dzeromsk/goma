// Copyright 2019 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/file_data_output.h"

#ifndef _WIN32
#include <errno.h>
#include <sys/stat.h>
#else
#include <shlobj.h>
#endif

#include <stack>

#include "glog/logging.h"
#include "lib/scoped_fd.h"

namespace devtools_goma {

namespace {

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

class FileOutputImpl : public FileDataOutput {
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

  bool IsValid() const override { return fd_.valid(); }
  bool WriteAt(off_t offset, const string& content) override {
    off_t pos = fd_.Seek(offset, devtools_goma::ScopedFd::SeekAbsolute);
    if (pos < 0 || pos != offset) {
      PLOG(ERROR) << "seek failed? " << filename_ << " pos=" << pos
                  << " offset=" << offset;
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

  string ToString() const override { return filename_; }

 private:
  const string filename_;
  devtools_goma::ScopedFd fd_;
  bool error_;
  DISALLOW_COPY_AND_ASSIGN(FileOutputImpl);
};

class StringOutputImpl : public FileDataOutput {
 public:
  StringOutputImpl(string name, string* buf)
      : name_(std::move(name)), buf_(buf), size_(0UL) {}
  ~StringOutputImpl() override {}

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

/* static */
std::unique_ptr<FileDataOutput> FileDataOutput::NewFileOutput(
    const string& filename,
    int mode) {
  return std::unique_ptr<FileDataOutput>(new FileOutputImpl(filename, mode));
}

/* static */
std::unique_ptr<FileDataOutput> FileDataOutput::NewStringOutput(
    const string& name,
    string* buf) {
  return std::unique_ptr<FileDataOutput>(new StringOutputImpl(name, buf));
}

}  // namespace devtools_goma
