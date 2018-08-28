// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_COMPRESS_UTIL_H_
#define DEVTOOLS_GOMA_LIB_COMPRESS_UTIL_H_

#include <memory>
#include <string>


#include "absl/strings/string_view.h"
#include "google/protobuf/io/gzip_stream.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#ifdef ENABLE_LZMA
# ifdef _WIN32
#  define LZMA_API_STATIC
# endif  // _WIN32
#include "lzma.h"
#endif  // ENABLE_LZMA

namespace devtools_goma {


using google::protobuf::int64;
using google::protobuf::io::ArrayInputStream;
using google::protobuf::io::ConcatenatingInputStream;
using google::protobuf::io::ZeroCopyInputStream;
using google::protobuf::io::ZeroCopyOutputStream;
using std::string;

enum class EncodingType {
  NO_ENCODING,
  DEFLATE,
  GZIP,
  LZMA2,
};

const char* GetEncodingName(EncodingType type);

// Parse encoding name.
// Note: it ignores weight.
EncodingType ParseEncodingName(absl::string_view s);

// Parse encoding in header field value.
// Note: it ignores weight, and can't handle '*'.
std::vector<EncodingType> ParseAcceptEncoding(absl::string_view field);

// Pick preferred encodings from accepts.
EncodingType PickEncoding(const std::vector<EncodingType>& accepts,
                          const std::vector<EncodingType>& prefs);

// Gets encoding type from |header| field value.
// If multiple encodings are found,
// this function returns the preferred one.
ABSL_DEPRECATED("Use ParseEncodingName, ParseAcceptEncoding and PickEncoding")
EncodingType GetEncodingFromHeader(absl::string_view header);


#ifdef ENABLE_LZMA
class LZMAInputStream : public ZeroCopyInputStream {
 public:
  explicit LZMAInputStream(std::unique_ptr<ZeroCopyInputStream> sub_stream);
  ~LZMAInputStream() override;

  LZMAInputStream(LZMAInputStream&&) = delete;
  LZMAInputStream(const LZMAInputStream&) = delete;
  LZMAInputStream& operator=(const LZMAInputStream&) = delete;
  LZMAInputStream& operator=(LZMAInputStream&&) = delete;

  // implements ZeroCopyInputStream ---
  bool Next(const void** data, int* size) override;
  void BackUp(int count) override;
  bool Skip(int size) override;
  int64 ByteCount() const override;

 private:
  lzma_ret Decode();
  void DoNextOutput(const void** data, int* size);

  std::unique_ptr<ZeroCopyInputStream> sub_stream_;

  // This code use lzma_context_ and lzma_error_ like GzipInputStream
  // in protobuf library.
  // lzma_context_ is used like zcontext_ in GzipInputStream, and
  // lzma_error_ is used like zerror_ in GzipInputStream.
  // lzma_context_ has following members:
  // - next_out: an address to be written by lzma_code in the next time.
  // - avail_out: size of next_out lzma_code can write in the next time.
  // - next_in: an address to be read by lzma_code in the next time.
  // - avail_in: size of next_in lzma_code can read in the next time.
  // Note that when lzma_code is called, above values are updated for the next
  // lzma_code. i.e. next_out increase and avail_out decrease with the size
  // of decompressed data.
  //
  // output_buffer_ is a buffer dynamically allocated for writing uncompressed
  // data.  output_buffer_size_ represents its size.
  // TODO: allow to change buffer size.
  //
  // output_position_ is a cursor in output_buffer_ to be read with Next method.
  // If BackUp is called, output_position_ decrease.
  lzma_stream lzma_context_;
  lzma_ret lzma_error_;
  std::unique_ptr<uint8_t[]> output_buffer_;
  size_t output_buffer_size_;
  uint8_t* output_position_;
  int64 byte_count_;
};

// LZMAOutputStream is an ZeroCopyOutputStream that compresses data to
// an underlying ZeroCopyOutputStream.
class LZMAOutputStream : public ZeroCopyOutputStream {
 public:
  struct Options {
    Options();

    uint32_t preset;
    lzma_check check;
    size_t buffer_size;
  };
  explicit LZMAOutputStream(std::unique_ptr<ZeroCopyOutputStream> sub_stream);
  LZMAOutputStream(std::unique_ptr<ZeroCopyOutputStream> sub_stream,
                   const Options& options);
  ~LZMAOutputStream() override;

  LZMAOutputStream(LZMAOutputStream&&) = delete;
  LZMAOutputStream(const LZMAOutputStream&) = delete;
  LZMAOutputStream& operator=(const LZMAOutputStream&) = delete;
  LZMAOutputStream& operator=(LZMAOutputStream&&) = delete;

  // Writes out all data and close the LZMA stream.
  // It is the caller's responsibility to close the underlying stream if
  // necessary.
  // Returns true if no error.
  bool Close();

  int ErrorCode() const {
    return lzma_error_;
  }

  // implements ZeroCopyOutputStream ---
  bool Next(void** data, int* size) override;
  void BackUp(int count) override;
  int64 ByteCount() const override;

 private:
  void Init(std::unique_ptr<ZeroCopyOutputStream> sub_stream,
            const Options& options);
  lzma_ret Encode(lzma_action action);
  void DoNextOutput(const void** data, int* size);

  std::unique_ptr<ZeroCopyOutputStream> sub_stream_;
  // Result from calling Next() on sub_stream_
  void* sub_data_;
  int sub_data_size_;

  // This code use lzma_context_ and lzma_error_ like GzipOutputStream
  // in protobuf library.
  // lzma_context_ is used like zcontext_ in GzipOutputStream, and
  // lzma_error_ is used like zerror_ in GzipOutputStream.
  // lzma_context_ has following members:
  // - next_out: an address to be written by lzma_code in the next time.
  // - avail_out: size of next_out lzma_code can write in the next time.
  // - next_in: an address to be read by lzma_code in the next time.
  // - avail_in: size of next_in lzma_code can read in the next time.
  // Note that when lzma_code is called, above values are updated for the next
  // lzma_code. i.e. next_out increase and avail_out decrease with the size
  // of decompressed data.
  lzma_stream lzma_context_;
  lzma_ret lzma_error_;

  // input_buffer_ is a buffer dynamically allocated for getting uncompressed
  // data.  input_buffer_size_ represents its size.
  std::unique_ptr<uint8_t[]> input_buffer_;
  size_t input_buffer_length_;
};

#endif

// InflateInputStream assumes sub_stream as deflate compressed stream.
// It automatically inserts zlib header to make sub_stream handled by
// GzipInputStream.
// TODO: insert zlib header only if needed.
class InflateInputStream : public ZeroCopyInputStream {
 public:
  explicit InflateInputStream(std::unique_ptr<ZeroCopyInputStream> sub_stream);
  ~InflateInputStream() override = default;

  InflateInputStream(InflateInputStream&&) = delete;
  InflateInputStream(const InflateInputStream&) = delete;
  InflateInputStream& operator=(const InflateInputStream&) = delete;
  InflateInputStream& operator=(InflateInputStream&&) = delete;

  // implements ZeroCopyInputStream ---
  bool Next(const void** data, int* size) override {
    return zlib_stream_->Next(data, size);
  }
  void BackUp(int count) override {
    zlib_stream_->BackUp(count);
  }
  bool Skip(int size) override {
    return zlib_stream_->Skip(size);
  }
  int64 ByteCount() const override {
    return zlib_stream_->ByteCount();
  }

 private:
  std::unique_ptr<ZeroCopyInputStream> zlib_stream_;

  std::unique_ptr<ZeroCopyInputStream> sub_stream_;
  std::vector<ZeroCopyInputStream*> sub_stream_inputs_;
  std::unique_ptr<ZeroCopyInputStream> zlib_header_;
  std::unique_ptr<ZeroCopyInputStream> zlib_content_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPRESS_UTIL_H_
