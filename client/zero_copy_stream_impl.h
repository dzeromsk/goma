// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_ZERO_COPY_STREAM_IMPL_H_
#define DEVTOOLS_GOMA_CLIENT_ZERO_COPY_STREAM_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include "compiler_specific.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "google/protobuf/io/gzip_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
MSVC_POP_WARNING()
#include "scoped_fd.h"
#include "zlib.h"

using std::string;

namespace devtools_goma {

// StringInputStream is helper for ArrayInputStream.
// It owns input string, so no need for caller to own the string
// along with input stream.
class StringInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  explicit StringInputStream(string data);
  ~StringInputStream() override = default;

  bool Next(const void** data, int* size) override {
    return array_stream_->Next(data, size);
  }
  void BackUp(int count) override { array_stream_->BackUp(count); }
  bool Skip(int count) override { return array_stream_->Skip(count); }
  google::protobuf::int64 ByteCount() const override {
    return array_stream_->ByteCount();
  }

 private:
  const string input_data_;
  std::unique_ptr<google::protobuf::io::ArrayInputStream> array_stream_;
};

// ScopedFdInputStream is similar with FileInputStream,
// but it uses ScopedFd instead of file descriptor.
// It owns ScopedFd, so it will be closed when the stream is
// destroyed by default.
class ScopedFdInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  explicit ScopedFdInputStream(ScopedFd fd);
  ~ScopedFdInputStream() override = default;

  bool Next(const void** data, int* size) override {
    return impl_.Next(data, size);
  }
  void BackUp(int count) override { impl_.BackUp(count); }
  bool Skip(int count) override { return impl_.Skip(count); }
  google::protobuf::int64 ByteCount() const override {
    return impl_.ByteCount();
  }

 private:
  class CopyingScopedFdInputStream
      : public google::protobuf::io::CopyingInputStream {
   public:
    explicit CopyingScopedFdInputStream(ScopedFd fd);
    ~CopyingScopedFdInputStream() override = default;

    int Read(void* buffer, int size) override;
    int Skip(int count) override;

   private:
    ScopedFd fd_;
  };
  CopyingScopedFdInputStream copying_input_;
  google::protobuf::io::CopyingInputStreamAdaptor impl_;
};

// ChainedInputStream is similar to ContatinatingInputStream,
// but it owns all underlying input streams.
class ChainedInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  explicit ChainedInputStream(
      std::vector<std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>>
          streams);
  ~ChainedInputStream() override = default;

  bool Next(const void** data, int* size) override {
    return concat_stream_->Next(data, size);
  }
  void BackUp(int count) override { concat_stream_->BackUp(count); }
  bool Skip(int count) override { return concat_stream_->Skip(count); }
  google::protobuf::int64 ByteCount() const override {
    return concat_stream_->ByteCount();
  }

 private:
  std::vector<std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>>
      input_streams_;
  std::vector<google::protobuf::io::ZeroCopyInputStream*> input_streams_array_;
  std::unique_ptr<google::protobuf::io::ConcatenatingInputStream>
      concat_stream_;
};

// GzipInputStream is similar with google::protobuf::io::GzipInputStream,
// but it owns input stream, and always uses GZIP stream.
class GzipInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  explicit GzipInputStream(std::unique_ptr<ZeroCopyInputStream> input);
  ~GzipInputStream() override = default;

  const char* ZlibErrorMessage() const {
    return gzip_stream_->ZlibErrorMessage();
  }
  int ZlibErrorCode() const { return gzip_stream_->ZlibErrorCode(); }

  bool Next(const void** data, int* size) override {
    return gzip_stream_->Next(data, size);
  }
  void BackUp(int count) override { gzip_stream_->BackUp(count); }
  bool Skip(int count) override { return gzip_stream_->Skip(count); }
  google::protobuf::int64 ByteCount() const override {
    return gzip_stream_->ByteCount();
  }

 private:
  std::unique_ptr<google::protobuf::io::GzipInputStream> gzip_stream_;
  std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> input_;
};

// GzipRequestInputStream compresses the input data in chunked encoding.
// Can be used for HTTP request body.
class GzipRequestInputStream
    : public google::protobuf::io::ZeroCopyInputStream {
 public:
  struct Options {
    int compression_level = Z_DEFAULT_COMPRESSION;
  };

  GzipRequestInputStream(
      std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> raw_data,
      Options options);
  ~GzipRequestInputStream() override = default;

  GzipRequestInputStream(GzipRequestInputStream&&) = delete;
  GzipRequestInputStream(const GzipRequestInputStream&) = default;
  GzipRequestInputStream& operator=(const GzipRequestInputStream&) = delete;
  GzipRequestInputStream& operator=(GzipRequestInputStream&&) = delete;

  bool Next(const void** data, int* size) override {
    return impl_.Next(data, size);
  }
  void BackUp(int count) override { impl_.BackUp(count); }
  bool Skip(int size) override { return impl_.Skip(size); }
  google::protobuf::int64 ByteCount() const override {
    return impl_.ByteCount();
  }

 private:
  class CopyingStream : public google::protobuf::io::CopyingInputStream {
   public:
    CopyingStream(
        std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> raw_data,
        Options options);
    ~CopyingStream() override;

    int Read(void* buffer, int size) override;

   private:
    std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> raw_data_;
    z_stream zcontext_;
    int zerror_ = Z_OK;
  };

  CopyingStream copy_input_;
  google::protobuf::io::CopyingInputStreamAdaptor impl_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_ZERO_COPY_STREAM_IMPL_H_
