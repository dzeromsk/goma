// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// FileReaderFactory is a factory class of FileReader and its subclass.
// You can register creator function of a special purpose FileReader subclass
// through Register. For registering creator function at the beginning of the
// program, FileReaderFactory is a singleton class.
// This class is thread-hostile.
//
// FileReader is a wrapper class of ScopedFd.
// Subclass of this class is made for giving a special behavior on reading.
// This class's thread safety is the same as scoped_fd.

#ifndef DEVTOOLS_GOMA_LIB_FILE_READER_H_
#define DEVTOOLS_GOMA_LIB_FILE_READER_H_

#include <memory>
#include <string>
#include <vector>


#include "scoped_fd.h"
using std::string;

namespace devtools_goma {

class FileReader;

class FileReaderFactory {
 public:
  // A type to create an FileReader instance.
  // It returns NULL if it cannot handle the given |filename|.
  typedef std::unique_ptr<FileReader> (*CreateFunction)(const string& filename);

  // Returns a new instance of FileReader or its subclass.
  std::unique_ptr<FileReader> NewFileReader(const string& filename);

  // Registers the creator functions of FileReader or its subclass.
  static void Register(CreateFunction create);

  // Gets the singleton instance of FileReaderFactory.
  static FileReaderFactory* GetInstance();

 private:
  FileReaderFactory() {}

  // Deletes the singleton instance to be called by atexit.
  static void DeleteInstance();

  std::vector<CreateFunction> creators_;
  static FileReaderFactory* factory_;

  DISALLOW_COPY_AND_ASSIGN(FileReaderFactory);
};

// Wrapper class of ScopedFd.
// Subclass of this class is used for special treatment of files.
class FileReader {
 public:
  virtual ~FileReader() {}

  // Wrapper of ScopedFd's Read.
  // If |len| == 0, returns 0.
  virtual ssize_t Read(void* ptr, size_t len) {
    if (len == 0) {
      return 0;
    }
    return fd_.Read(ptr, len);
  }

  // Wrapper of ScopedFd's Seek.
  virtual off_t Seek(off_t offset, ScopedFd::Whence whence) const {
    return fd_.Seek(offset, whence);
  }

  // Wrapper of ScopedFd's valid.
  virtual bool valid() const {
    return fd_.valid();
  }

  // Wrapper of ScopedFd's GetFileSize.
  virtual bool GetFileSize(size_t* file_size) const {
    return fd_.GetFileSize(file_size);
  }

  // Copies data in |*buf| to |*ptr| with |*len|.
  // |*ptr| is automatically incremented and |*len| is automatically
  // decremented. Moved data in |*buf| is removed.
  // Returns the number of copied bytes.
  //
  // Note: if size of |*buf| is larger than |*len|, copy would happen,
  //       performance may suffer.
  static size_t FlushDataInBuffer(string* buf, void** ptr, size_t* len);

 protected:
  explicit FileReader(const string& filename)
      : fd_(ScopedFd::OpenForRead(filename)) {
  }

 private:
  // Returns an instance of FileReader.
  static std::unique_ptr<FileReader> Create(const string& filename) {
    return std::unique_ptr<FileReader>(new FileReader(filename));
  }

  ScopedFd fd_;

  friend class FileReaderFactory;
  DISALLOW_COPY_AND_ASSIGN(FileReader);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_FILE_READER_H_
