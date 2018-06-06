// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jarfile_reader.h"

#include <cstring>

#include "absl/strings/match.h"
#include "basictypes.h"
#include "glog/logging.h"

namespace {

static uint16_t ToUInt16(const char* ptr) {
  return static_cast<uint8_t>(ptr[0]) | (static_cast<uint8_t>(ptr[1]) << 8);
}

static uint32_t ToUInt32(const char* ptr) {
  return static_cast<uint8_t>(ptr[0]) | (static_cast<uint8_t>(ptr[1]) << 8) |
         (static_cast<uint8_t>(ptr[2]) << 16) |
         (static_cast<uint8_t>(ptr[3]) << 24);
}

}  // namespace

namespace devtools_goma {

/* static */
std::unique_ptr<FileReader> JarFileReader::Create(const std::string& filename) {
  if (!CanHandle(filename)) {
    return nullptr;
  }
  std::unique_ptr<JarFileReader> file_reader(new JarFileReader(filename));
  if (!file_reader->valid() || file_reader->detected_zip_normalized_time()) {
    return nullptr;
  }
  // TODO: vlog if too chatty.
  // Since the number of jar files should not be large, and we see the message
  // once compiler_proxy read the file.  I guess it not so chatty.
  LOG(INFO) << "JarFileReader is used. filename=" << filename;
  return std::move(file_reader);
}

/* static */
bool JarFileReader::CanHandle(const std::string& filename) {
  return absl::EndsWith(filename, ".jar");
}

JarFileReader::JarFileReader(const std::string& filename)
    : FileReader(filename),
      buffer_head_pos_(0),
      last_normalized_absolute_pos_(0),
      is_buffer_normalized_(false),
      is_central_directory_started_(false),
      is_valid_(false),
      detected_zip_normalized_time_(false),
      offset_(0),
      input_filename_(filename) {
  buffer_.resize(0x30);
  if (FileReader::Read(&buffer_[0], buffer_.size()) != buffer_.size()) {
    return;
  }
  // If the file looks like ZIP archive, it might be ok to normalize.
  // Some jar files used by Android build seems not be valid jar file but
  // we allow jarfile reader to normalize it if it looks like zip file.
  // (b/38329025)
  if (absl::StartsWith(buffer_, "PK\x03\x04")) {
    is_valid_ = true;
  }
  // Checks the Jar file magic string (0xcafe) existence.
  // I am not confident we can normalize a broken jar file, and ease of
  // finding such a file, let me log.
  LOG_IF(WARNING, ToUInt16(buffer_.data() + 0x27) != 0xcafe)
      << "JarFileReader: the file seems not have jar file magic:"
      << "expect 0xcafe (little endian) but " << std::hex
      << ToUInt16(buffer_.data() + 0x27)
      << " input_filename=" << input_filename_;

  // If ziptime has already been applied, we do not need to normalize.
  //
  // See also:
  // https://android.googlesource.com/platform/build/+/master/tools/ziptime/ZipEntry.cpp
  // kZipTimeStaticDate come from ziptime code above, and it is 2008-01-01.
  // date format: (year - 1980) << 9 | month << 5 | day.
  static uint16_t kZipTimeStaticDate = (2008 - 1980) << 9 | 1 << 5 | 1;
  static uint16_t kZipTimeStaticTime = 0;
  if (ToUInt16(buffer_.data() + 0x0a) == kZipTimeStaticTime &&
      ToUInt16(buffer_.data() + 0x0c) == kZipTimeStaticDate) {
    LOG(INFO) << "JarFileReader won't normalize jar file that has already been"
              << " normalized with ziptime."
              << " input_filename=" << input_filename_;
    detected_zip_normalized_time_ = true;
    return;
  }
  // TODO: skip normalize prebuilt jar files.
  // Currently, we also normalizes prebuilt library jar files.
  // Since such files are also stored in output directory, it is difficult to
  // distinguish.

  NormalizeBuffer();
}

ssize_t JarFileReader::ReadDataToBuffer(size_t size) {
  size_t orig_size = buffer_.size();
  buffer_.resize(orig_size + size);
  ssize_t read_bytes = FileReader::Read(&buffer_[orig_size], size);
  if (read_bytes >= 0) {
    buffer_.resize(orig_size + read_bytes);
  }
  VLOG(2) << "input_filename=" << input_filename_
          << " read buffer_.size()=" << buffer_.size() << " size=" << size
          << " read_bytes=" << read_bytes;
  return read_bytes;
}

// NormalizeBuffer normalizes a timestamp in the header.
//
// How it works?
// 1. find "PK".
// 2. signature starting from "PK" let us know what header is there, and
//    normalize a timestamp in it.
//
// Serious way of parsing .jar file is uncompressing each ZIP entry until
// the end of compressed data.  This is what the original jar command do.
// However, as far as I am inspired by zlib/contrib/minizip/unzip.c,
// just skipping to the signature seems to usually work.
//
// See Also: https://en.wikipedia.org/wiki/Zip_(file_format)#File_headers
// Note that header structure is the same between ZIP and ZIP64.
void JarFileReader::NormalizeBuffer() {
  DCHECK_LE(buffer_head_pos_, last_normalized_absolute_pos_)
      << "buffer_head_pos must be smaller than or equals to "
      << "last_normalized_absolute_pos_"
      << " input_filename=" << input_filename_
      << " buffer_head_pos=" << buffer_head_pos_
      << " last_normalized_absolute_pos=" << last_normalized_absolute_pos_;
  // Normalize the buffer from the last normalized position.
  size_t cur = last_normalized_absolute_pos_ - buffer_head_pos_;
  is_buffer_normalized_ = true;
  for (;;) {
    cur = buffer_.find("PK", cur);
    if (cur == string::npos) {
      // 'K' may come just after 'P'.  Let me mark this not normalized.
      if (!buffer_.empty() && buffer_[buffer_.size() - 1] == 'P') {
        is_buffer_normalized_ = false;
      } else {
        last_normalized_absolute_pos_ = buffer_head_pos_ + buffer_.size();
      }
      return;
    }
    if (cur + 4 > buffer_.size()) {
      // Will cause buffer overrun.
      VLOG(1) << "would cause buffer overrun."
              << " input_filename=" << input_filename_ << " cur=" << cur
              << " buffer_head_pos=" << buffer_head_pos_
              << " buffer_.size()=" << buffer_.size();
      is_buffer_normalized_ = false;
      return;
    }
    ssize_t offset = GetTimestampOffset(&buffer_[cur]);
    VLOG(3) << "offset:" << offset;
    if (offset < 0) {
      cur += 4;
      continue;
    }
    if (cur + offset + 4 > buffer_.size()) {
      // Will cause buffer overrun.
      VLOG(1) << "would cause buffer overrun."
              << " input_filename=" << input_filename_ << " cur=" << cur
              << " buffer_head_pos=" << buffer_head_pos_ << " offset=" << offset
              << " buffer_.size()=" << buffer_.size();
      is_buffer_normalized_ = false;
      return;
    }
    // Set timestamp to the epoch time. 1980-01-01T00:00:00
    // Note that all 0 represents 1980-00-00T00:00:00, which could be invalid.
    buffer_[cur + offset + 0] = 0;
    buffer_[cur + offset + 1] = 0;
    buffer_[cur + offset + 2] = 0x21;
    buffer_[cur + offset + 3] = 0;
    // offset from the head of the header + timestamp (4bytes) to go to just
    // next to timestamp.
    cur += offset + 4;
    last_normalized_absolute_pos_ = buffer_head_pos_ + cur;
  }
}

ssize_t JarFileReader::GetTimestampOffset(const char* signature) {
  // Please see also:
  // https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
  static const uint32_t kLocalFileHeaderSignature = 0x04034b50;
  static const uint32_t kCentralFileHeaderSignature = 0x02014b50;

  uint32_t u32_signature = ToUInt32(signature);
  VLOG(3) << "signature:" << std::hex << u32_signature
          << " input_filename=" << input_filename_
          << " buffer_head_pos=" << buffer_head_pos_
          << " last_normalized_absolute_pos=" << last_normalized_absolute_pos_
          << " offset=" << offset_ << " buffer_.size()=" << buffer_.size();
  if (u32_signature == kLocalFileHeaderSignature) {
    DCHECK(!is_central_directory_started_)
        << "Local file descriptor signature comes after central directory "
        << "entry."
        << " input_filename=" << input_filename_
        << " buffer_head_pos=" << buffer_head_pos_
        << " last_normalized_absolute_pos=" << last_normalized_absolute_pos_
        << " offset_=" << offset_;
    return 10;
  }
  if (u32_signature == kCentralFileHeaderSignature) {
    if (!is_central_directory_started_) {
      is_central_directory_started_ = true;
    }
    return 12;
  }
  return -1;
}

ssize_t JarFileReader::Read(void* ptr, size_t len) {
  // TODO: increase kBufSize when it works fine.
  // small buffer size is good for checking code but not good for real world.
  static const size_t kBufSize = 128;
  // https://en.wikipedia.org/wiki/Zip_(file_format)
  // Central directory file header should be the largest.
  static const size_t kMaxHeaderSize = 46;
  COMPILE_ASSERT(kBufSize > kMaxHeaderSize,
                 "Buffer size should be larger than ZIP header size.");

  off_t buffer_head_pos_at_beginning = buffer_head_pos_;
  if (is_buffer_normalized_) {
    buffer_head_pos_ += FileReader::FlushDataInBuffer(&buffer_, &ptr, &len);
  }
  while (len > 0) {
    ssize_t read_bytes = ReadDataToBuffer(kBufSize);
    if (read_bytes < 0) {  // Return error soon.
      return read_bytes;
    }
    if (read_bytes != kBufSize) {  // Should be end of the file.
      VLOG(1) << "input_filename=" << input_filename_
              << " buffer_head_pos=" << buffer_head_pos_
              << " buffer_.size()=" << buffer_.size();
      NormalizeBuffer();
      // No more data. i.e. no possibility that next buffer may contain the
      // data that need to be normalized.
      buffer_head_pos_ += FileReader::FlushDataInBuffer(&buffer_, &ptr, &len);
      break;
    }

    VLOG(1) << "input_filename=" << input_filename_
            << " buffer_head_pos=" << buffer_head_pos_
            << " buffer_.size()=" << buffer_.size();
    NormalizeBuffer();
    if (is_buffer_normalized_) {
      buffer_head_pos_ += FileReader::FlushDataInBuffer(&buffer_, &ptr, &len);
    }
  }

  size_t read_bytes = buffer_head_pos_ - buffer_head_pos_at_beginning;
  offset_ += read_bytes;
  VLOG(1) << "input_filename=" << input_filename_
          << " read_bytes=" << read_bytes << " offset_=" << offset_
          << " buffer_head_pos=" << buffer_head_pos_
          << " buffer_.size()=" << buffer_.size()
          << " is_buffer_normalized=" << is_buffer_normalized_;
  return read_bytes;
}

off_t JarFileReader::Seek(off_t offset, ScopedFd::Whence whence) const {
  CHECK_EQ(whence, ScopedFd::SeekAbsolute)
      << "Sorry, this function only support seek absolute";
  CHECK_EQ(offset, offset_)
      << "Sorry, this function expects the user set just next position.";
  return offset;
}

}  // namespace devtools_goma
