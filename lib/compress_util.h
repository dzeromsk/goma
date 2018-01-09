// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_COMPRESS_UTIL_H_
#define DEVTOOLS_GOMA_LIB_COMPRESS_UTIL_H_

#include <memory>
#include <string>


#include "string_piece.h"
#ifdef ENABLE_LZMA
# ifdef _WIN32
#  define LZMA_API_STATIC
# endif  // _WIN32
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/message.h"
#include "lzma.h"
#endif  // ENABLE_LZMA

namespace devtools_goma {


#ifdef ENABLE_LZMA
using google::protobuf::io::ZeroCopyInputStream;
using google::int64;
#endif
using std::string;

enum EncodingType {
  NO_ENCODING = 0,
  ENCODING_DEFLATE,
  ENCODING_LZMA2,
  NUM_ENCODINGS
};

const char* GetEncodingName(EncodingType type);

// Gets encoding type from |header|. If multiple encodings are found,
// this function returns the preferred one.
EncodingType GetEncodingFromHeader(const char* header);

#ifdef ENABLE_LZMA
bool ReadAllLZMAStream(absl::string_view input, lzma_stream* lzma,
                       string* output);

class LZMAInputStream : public ZeroCopyInputStream {
 public:
  explicit LZMAInputStream(ZeroCopyInputStream* sub_stream);
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

  ZeroCopyInputStream* sub_stream_;

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

#endif


}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_COMPRESS_UTIL_H_
