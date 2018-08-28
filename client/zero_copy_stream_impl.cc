// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "zero_copy_stream_impl.h"

#include <cstring>

#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "glog/logging.h"
#include "zlib.h"

namespace {

// same buffer size with google/protobuf/io/gzip_stream
constexpr int kDefaultBufferSize = 65536;

constexpr absl::string_view kChunkHeader("0000\r\n");
constexpr absl::string_view kChunkEnd("\r\n");

// FixChunk sets chunk-size and CRLF separator.
void FixChunk(void* buffer, int chunk_size) {
  memcpy(buffer, kChunkHeader.data(), kChunkHeader.size());

  CHECK_LE(chunk_size, 0xffff);
  string size_str = absl::StrCat(absl::Hex(chunk_size, absl::kZeroPad4));
  CHECK_EQ(size_str.size(), 4);
  memcpy(buffer, size_str.data(), size_str.size());

  memcpy(static_cast<char*>(buffer) + kChunkHeader.size() + chunk_size,
         kChunkEnd.data(), kChunkEnd.size());
  absl::string_view header(static_cast<const char*>(buffer),
                           kChunkHeader.size());
  VLOG(2) << absl::CEscape(header);
}

}  // anonymous namespace

namespace devtools_goma {

StringInputStream::StringInputStream(string data)
    : input_data_(std::move(data)),
      array_stream_(absl::make_unique<google::protobuf::io::ArrayInputStream>(
          input_data_.data(),
          input_data_.size())) {}

ScopedFdInputStream::ScopedFdInputStream(ScopedFd fd)
    : copying_input_(std::move(fd)),
      impl_(&copying_input_, /*block_size=*/-1) {}

ScopedFdInputStream::CopyingScopedFdInputStream::CopyingScopedFdInputStream(
    ScopedFd fd)
    : fd_(std::move(fd)) {}

int ScopedFdInputStream::CopyingScopedFdInputStream::Read(void* buffer,
                                                          int size) {
  return fd_.Read(buffer, size);
}

int ScopedFdInputStream::CopyingScopedFdInputStream::Skip(int count) {
  if (fd_.Seek(count, ScopedFd::SeekRelative) != static_cast<off_t>(-1)) {
    return count;
  }
  // Use the default implementation.
  return CopyingInputStream::Skip(count);
}

ChainedInputStream::ChainedInputStream(
    std::vector<std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>>
        streams)
    : input_streams_(std::move(streams)) {
  input_streams_array_.reserve(input_streams_.size());
  for (const auto& s : input_streams_) {
    input_streams_array_.push_back(s.get());
  }
  concat_stream_ =
      absl::make_unique<google::protobuf::io::ConcatenatingInputStream>(
          input_streams_array_.data(), input_streams_array_.size());
}

GzipInputStream::GzipInputStream(std::unique_ptr<ZeroCopyInputStream> input)
    : gzip_stream_(absl::make_unique<google::protobuf::io::GzipInputStream>(
          input.get(),
          google::protobuf::io::GzipInputStream::GZIP)),
      input_(std::move(input)) {}

GzipRequestInputStream::GzipRequestInputStream(
    std::unique_ptr<ZeroCopyInputStream> raw_data,
    Options options)
    : copy_input_(std::move(raw_data), options),
      impl_(&copy_input_, kDefaultBufferSize) {}

GzipRequestInputStream::CopyingStream::CopyingStream(
    std::unique_ptr<ZeroCopyInputStream> raw_data,
    Options options)
    : raw_data_(std::move(raw_data)) {
  zcontext_.state = Z_NULL;
  zcontext_.zalloc = Z_NULL;
  zcontext_.zfree = Z_NULL;
  zcontext_.opaque = Z_NULL;
  zcontext_.total_out = 0;
  zcontext_.next_in = nullptr;
  zcontext_.avail_in = 0;
  zcontext_.total_in = 0;
  zcontext_.msg = nullptr;
  zcontext_.next_out = nullptr;
  zcontext_.avail_out = 0;
  const int windowsBits = 15;
  const int windowsBitsFormat = 16;  // GZIP
  zerror_ = deflateInit2(&zcontext_, options.compression_level, Z_DEFLATED,
                         windowsBits | windowsBitsFormat,
                         /* memLevel (default) */ 8, Z_DEFAULT_STRATEGY);
  LOG_IF(ERROR, zerror_ != Z_OK)
      << "deflateInit2 failed " << zerror_ << " " << zError(zerror_);
}

GzipRequestInputStream::CopyingStream::~CopyingStream() {
  deflateEnd(&zcontext_);
}

int GzipRequestInputStream::CopyingStream::Read(void* buffer, int size) {
  DCHECK_EQ(size, kDefaultBufferSize);
  if (zerror_ == Z_STREAM_END) {
    return 0;  // EOF
  }
  if (zerror_ != Z_OK) {
    LOG(ERROR) << "Read zerror=" << zerror_ << " " << zError(zerror_);
    return -1;  // error
  }
  const void* raw_buffer = nullptr;
  int raw_size = 0;
  while (raw_size == 0) {
    if (!raw_data_->Next(&raw_buffer, &raw_size)) {
      // EOF or error
      // TODO: check bytecount of raw_data to see error or EOF?
      constexpr absl::string_view kLast("0\r\n\r\n");

      Bytef empty_input[1];
      zcontext_.next_in = empty_input;
      zcontext_.avail_in = 0;
      zcontext_.next_out = static_cast<Bytef*>(buffer) + kChunkHeader.size();
      Bytef* start = zcontext_.next_out;
      zcontext_.avail_out =
          size - kChunkHeader.size() - kChunkEnd.size() - kLast.size();
      CHECK_GT(zcontext_.avail_out, 0);
      zerror_ = deflate(&zcontext_, Z_FINISH);
      if (zerror_ != Z_STREAM_END) {
        LOG(ERROR) << "deflate finish error " << zerror_ << " "
                   << zError(zerror_);
        return -1;
      }
      int chunk_size = zcontext_.next_out - start;
      FixChunk(buffer, chunk_size);
      zcontext_.next_out += kChunkEnd.size();
      zcontext_.avail_out -= kChunkEnd.size();

      CHECK_GT(zcontext_.avail_out, kLast.size());
      memcpy(zcontext_.next_out, kLast.data(), kLast.size());
      return zcontext_.next_out + kLast.size() - static_cast<Bytef*>(buffer);
    }
    LOG_IF(INFO, raw_size == 0) << "Next returns size=0, retry";
  }
  int chunk_size = 0;
  zcontext_.next_in = static_cast<const Bytef*>(raw_buffer);
  zcontext_.avail_in = raw_size;
  zcontext_.next_out = static_cast<Bytef*>(buffer) + kChunkHeader.size();
  zcontext_.avail_out = size - kChunkHeader.size() - kChunkEnd.size();
  do {
    Bytef* start = zcontext_.next_out;
    zerror_ = deflate(&zcontext_, Z_NO_FLUSH);
    if (zerror_ != Z_OK) {
      LOG(ERROR) << "deflate error " << zerror_ << " " << zError(zerror_)
                 << " in=" << zcontext_.next_in << " " << zcontext_.avail_in
                 << " out=" << zcontext_.next_out << " " << zcontext_.avail_out;
      return -1;
    }
    VLOG(1) << "deflate size=" << (zcontext_.next_out - start);
    chunk_size += zcontext_.next_out - start;
  } while (zcontext_.avail_in > 0 && zcontext_.avail_out > 0);
  if (zcontext_.avail_in > 0) {
    VLOG(1) << "deflate backup=" << zcontext_.avail_in;
    raw_data_->BackUp(zcontext_.avail_in);
  }
  VLOG(1) << "deflate chunk_size=" << chunk_size;
  FixChunk(buffer, chunk_size);
  int ret = kChunkHeader.size() + chunk_size + kChunkEnd.size();
  VLOG(2) << absl::CEscape(
      absl::string_view(static_cast<const char*>(buffer), ret));
  return ret;
}

}  // namespace devtools_goma
