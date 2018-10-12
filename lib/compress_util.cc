// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/compress_util.h"

#include <string.h>

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "google/protobuf/io/gzip_stream.h"

using google::protobuf::io::GzipInputStream;

namespace {

#ifdef ENABLE_LZMA
const size_t kDefaultLZMAOutputBufSize = 65536;
#endif

}  // namespace

namespace devtools_goma {

const char* GetEncodingName(EncodingType type) {
  switch (type) {
    case EncodingType::NO_ENCODING:
      return "no encoding";
    case EncodingType::DEFLATE:
      return "deflate";
    case EncodingType::GZIP:
      return "gzip";
    case EncodingType::LZMA2:
      return "lzma2";
  }
}

EncodingType ParseEncodingName(absl::string_view s) {
  if (absl::StartsWith(s, "deflate")) {
    return EncodingType::DEFLATE;
  }
  if (absl::StartsWith(s, "gzip")) {
    return EncodingType::GZIP;
  }
  if (absl::StartsWith(s, "lzma2")) {
    return EncodingType::LZMA2;
  }
  return EncodingType::NO_ENCODING;
}

std::vector<EncodingType> ParseAcceptEncoding(absl::string_view field) {
  std::vector<EncodingType> ret;
  for (const auto& coding :
           absl::StrSplit(field, ',', absl::SkipWhitespace())) {
    ret.push_back(ParseEncodingName(absl::StripAsciiWhitespace(coding)));
  }
  return ret;
}

EncodingType PickEncoding(const std::vector<EncodingType>& accepts,
                          const std::vector<EncodingType>& prefs) {
  for (const auto& encoding : prefs) {
    for (const auto& e : accepts) {
      if (encoding == e) {
        return encoding;
      }
    }
  }
  return EncodingType::NO_ENCODING;
}

EncodingType GetEncodingFromHeader(absl::string_view header) {
  std::vector<EncodingType> prefs = {
    EncodingType::LZMA2,
    EncodingType::GZIP,
    EncodingType::DEFLATE,
  };
  return PickEncoding(ParseAcceptEncoding(header), prefs);
}

#ifdef ENABLE_LZMA
LZMAInputStream::LZMAInputStream(
    std::unique_ptr<ZeroCopyInputStream> sub_stream)
    : sub_stream_(std::move(sub_stream)),
      lzma_context_(LZMA_STREAM_INIT), lzma_error_(LZMA_OK),
      byte_count_(0) {
  lzma_context_.next_in = nullptr;
  lzma_context_.avail_in = 0;

  output_buffer_size_ = kDefaultLZMAOutputBufSize;
  output_buffer_.reset(new uint8_t[output_buffer_size_]);
  lzma_context_.next_out = output_buffer_.get();
  lzma_context_.avail_out = output_buffer_size_;
  output_position_ = output_buffer_.get();
}

LZMAInputStream::~LZMAInputStream() {
  lzma_end(&lzma_context_);
}

lzma_ret LZMAInputStream::Decode() {
  if (lzma_error_ == LZMA_OK && lzma_context_.avail_out == 0) {
    // previous decode filled buffer. don't change input params yet.
  } else if (lzma_context_.avail_in == 0) {
    const void* in;
    int in_size;
    bool first = lzma_context_.next_in == nullptr;
    bool ok = sub_stream_->Next(&in, &in_size);
    if (!ok) {
      lzma_context_.next_out = nullptr;
      lzma_context_.avail_out = 0;
      return LZMA_STREAM_END;
    }
    lzma_context_.next_in = reinterpret_cast<const uint8_t*>(in);
    lzma_context_.avail_in = in_size;
    if (first) {
      lzma_ret error = lzma_stream_decoder(&lzma_context_,
                                           lzma_easy_decoder_memusage(9),
                                           0);
      if (error != LZMA_OK) {
        return error;
      }
    }
  }
  lzma_context_.next_out = reinterpret_cast<uint8_t*>(output_buffer_.get());
  lzma_context_.avail_out = output_buffer_size_;
  output_position_ = output_buffer_.get();
  return lzma_code(&lzma_context_, LZMA_RUN);
}

void LZMAInputStream::DoNextOutput(const void** data, int* size) {
  *data = output_position_;
  *size = lzma_context_.next_out - output_position_;
  output_position_ = lzma_context_.next_out;
}

bool LZMAInputStream::Next(const void** data, int* size) {
  bool ok = ((lzma_error_ == LZMA_OK) || (lzma_error_ == LZMA_STREAM_END) ||
             (lzma_error_ == LZMA_BUF_ERROR));
  if (!ok || (lzma_context_.next_out == nullptr)) {
    return false;
  }
  if (lzma_context_.next_out != output_position_) {
    DoNextOutput(data, size);
    return true;
  }
  if (lzma_error_ == LZMA_STREAM_END) {
    if (lzma_context_.next_out == nullptr) {
      *data = nullptr;
      *size = 0;
      return false;
    } else {
      // TODO: consider to use lzma's concatenated stream support?
      // sub_stream_ may have concatenated streams to follow.
      lzma_end(&lzma_context_);
      byte_count_ += lzma_context_.total_out;
      lzma_error_ = lzma_stream_decoder(&lzma_context_,
                                        lzma_easy_decoder_memusage(9),
                                        0);
      if (lzma_error_ != LZMA_OK) {
        return false;
      }
    }
  }
  lzma_error_ = Decode();
  if (lzma_error_ == LZMA_STREAM_END && lzma_context_.next_out == nullptr) {
    // The underlying stream's Next returned false inside Decode.
    return false;
  }
  ok = ((lzma_error_ == LZMA_OK) || (lzma_error_ == LZMA_STREAM_END) ||
        (lzma_error_ == LZMA_BUF_ERROR));
  if (!ok) {
    return false;
  }
  DoNextOutput(data, size);
  return true;
}

void LZMAInputStream::BackUp(int count)  {
  output_position_ -= count;
  CHECK(output_position_ > output_buffer_.get());
}

bool LZMAInputStream::Skip(int count) {
  const void* data;
  int size;
  bool ok = false;
  while ((ok = Next(&data, &size)) && (size < count)) {
    count -= size;
  }
  if (ok && (size > count)) {
    BackUp(size - count);
  }
  return ok;
}

int64 LZMAInputStream::ByteCount() const {
  int ret = byte_count_ + lzma_context_.total_out;
  if (lzma_context_.next_out != nullptr && output_position_ != nullptr) {
    // GzipInputStream adds followings but I think we need to remove.
    //
    // Followings won't be 0 if BackUp is called.  In such a case,
    // total_out contains the bytes it is pushed back by BackUp.
    ret -= reinterpret_cast<uintptr_t>(lzma_context_.next_out) -
        reinterpret_cast<uintptr_t>(output_position_);
  }
  return ret;
}

LZMAOutputStream::Options::Options()
    : preset(LZMA_PRESET_DEFAULT), check(LZMA_CHECK_CRC64),
      buffer_size(kDefaultLZMAOutputBufSize) {
}

LZMAOutputStream::LZMAOutputStream(
    std::unique_ptr<ZeroCopyOutputStream> sub_stream) {
  LZMAOutputStream::Options options;
  Init(std::move(sub_stream), options);
}

LZMAOutputStream::LZMAOutputStream(
    std::unique_ptr<ZeroCopyOutputStream> sub_stream, const Options& options) {
  Init(std::move(sub_stream), options);
}

LZMAOutputStream::~LZMAOutputStream() {
  lzma_end(&lzma_context_);
}

void LZMAOutputStream::Init(std::unique_ptr<ZeroCopyOutputStream> sub_stream,
                            const Options& options) {
  sub_stream_ = std::move(sub_stream);
  sub_data_ = nullptr;
  sub_data_size_ = 0;

  input_buffer_length_ = options.buffer_size;
  CHECK_GT(input_buffer_length_, 0);
  input_buffer_ = absl::make_unique<uint8_t[]>(input_buffer_length_);
  CHECK(input_buffer_ != nullptr);

  // LZMA_STREAM_INIT clears all fields, we do not need to clear them by
  // ourselves.
  lzma_context_ = LZMA_STREAM_INIT;
  lzma_error_ = lzma_easy_encoder(&lzma_context_,
                                  options.preset,
                                  options.check);
}

lzma_ret LZMAOutputStream::Encode(lzma_action action) {
  lzma_ret error = LZMA_OK;
  do {
    if (sub_data_ == nullptr || lzma_context_.avail_out == 0) {
      bool ok = sub_stream_->Next(&sub_data_, &sub_data_size_);
      if (!ok) {
        sub_data_ = nullptr;
        sub_data_size_ = 0;
        return LZMA_BUF_ERROR;
      }
      CHECK_GT(sub_data_size_, 0);
      lzma_context_.next_out = static_cast<uint8_t*>(sub_data_);
      lzma_context_.avail_out = sub_data_size_;
    }
    error = lzma_code(&lzma_context_, action);
  } while (error == LZMA_OK && lzma_context_.avail_out == 0);
  if (action == LZMA_FULL_FLUSH || action == LZMA_FINISH) {
    // Notify lower layer of data.
    sub_stream_->BackUp(lzma_context_.avail_out);
    // We don't own the buffer any more.
    sub_data_ = nullptr;
    sub_data_size_ = 0;
  }
  return error;
}

bool LZMAOutputStream::Next(void** data, int* size) {
  if (lzma_error_ != LZMA_OK && lzma_error_ == LZMA_BUF_ERROR) {
    return false;
  }
  if (lzma_context_.avail_in != 0) {
    lzma_error_ = Encode(LZMA_RUN);
    if (lzma_error_ != LZMA_OK) {
      return false;
    }
  }
  if (lzma_context_.avail_in == 0) {
    VLOG(3) << "updated avail_in"
            << " size=" << input_buffer_length_;
    lzma_context_.next_in = input_buffer_.get();
    lzma_context_.avail_in = input_buffer_length_;
    *data = input_buffer_.get();
    *size = input_buffer_length_;
  } else {
    LOG(DFATAL) << "lzma left bytes unconsumed";
  }

  return true;
}

void LZMAOutputStream::BackUp(int count) {
  CHECK_GE(lzma_context_.avail_in, count);
  lzma_context_.avail_in -= count;
}

int64 LZMAOutputStream::ByteCount() const {
  return lzma_context_.total_in + lzma_context_.avail_in;
}

bool LZMAOutputStream::Close() {
  if (lzma_error_ != LZMA_OK && lzma_error_ != LZMA_BUF_ERROR) {
    return false;
  }
  do {
    lzma_error_ = Encode(LZMA_FINISH);
  } while (lzma_error_ == LZMA_OK);
  return lzma_error_ == LZMA_STREAM_END;
}

#endif

InflateInputStream::InflateInputStream(
    std::unique_ptr<ZeroCopyInputStream> sub_stream)
    : zlib_content_(std::move(sub_stream)) {
  // see chrome/src/net/filter/gzip_source_stream.cc InsertZlibHeader.
  static const char kZlibHeader[2] = {0x78, 0x01};
  zlib_header_ = absl::make_unique<ArrayInputStream>(kZlibHeader, 2);
  sub_stream_inputs_.push_back(zlib_header_.get());
  sub_stream_inputs_.push_back(zlib_content_.get());
  sub_stream_ =
      absl::make_unique<ConcatenatingInputStream>(
          &sub_stream_inputs_[0], sub_stream_inputs_.size());

  zlib_stream_ = absl::make_unique<GzipInputStream>(
      sub_stream_.get(), GzipInputStream::ZLIB);
}

}  // namespace devtools_goma
