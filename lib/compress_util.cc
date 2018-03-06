// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "compress_util.h"

#include <string.h>

#include "absl/strings/string_view.h"
#include "glog/logging.h"

namespace {

#ifdef ENABLE_LZMA
const size_t kDefaultLZMAOutputBufSize = 65536;
#endif

}  // namespace

namespace devtools_goma {

const char* const kEncodingNames[NUM_ENCODINGS] = {
  "no encoding",
  "deflate",
  "lzma2",
};

const char* GetEncodingName(EncodingType type) {
  DCHECK_GE(type, NO_ENCODING);
  DCHECK_LT(type, NUM_ENCODINGS);
  return kEncodingNames[type];
}

EncodingType GetEncodingFromHeader(const char* header) {
  if (!header) {
    return NO_ENCODING;
  }
  if (strstr(header, "lzma2")) {
    return ENCODING_LZMA2;
  }
  if (strstr(header, "deflate")) {
    return ENCODING_DEFLATE;
  }
  return NO_ENCODING;
}

#ifdef ENABLE_LZMA
bool ReadAllLZMAStream(absl::string_view input, lzma_stream* lzma,
                       string* output) {
  lzma->next_in = reinterpret_cast<const uint8_t*>(input.data());
  lzma->avail_in = input.size();
  char buf[4096];
  lzma->next_out = reinterpret_cast<uint8_t*>(buf);
  lzma->avail_out = sizeof(buf);
  bool is_success = true;
  for (;;) {
    lzma_ret r = lzma_code(lzma, LZMA_FINISH);
    output->append(buf, sizeof(buf) - lzma->avail_out);
    if (r == LZMA_OK) {
      lzma->next_out = reinterpret_cast<uint8_t*>(buf);
      lzma->avail_out = sizeof(buf);
    } else {
      if (LZMA_STREAM_END != r) {
        LOG(DFATAL) << r;
        is_success = false;
        break;
      }
      break;
    }
  }
  lzma_end(lzma);
  return is_success;
}

LZMAInputStream::LZMAInputStream(ZeroCopyInputStream* sub_stream)
    : sub_stream_(sub_stream),
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

#endif

}  // namespace devtools_goma
