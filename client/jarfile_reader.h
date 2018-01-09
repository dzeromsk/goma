// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_JARFILE_READER_H_
#define DEVTOOLS_GOMA_CLIENT_JARFILE_READER_H_

#include <memory>
#include <string>

#include "file_reader.h"
#include "gtest/gtest_prod.h"

namespace devtools_goma {

// A subclass of FileReader to normalize Java jar file during reading.
//
// TODO: may consider serious implementation if needed.
//
// Limitation:
// The normalization will be done with heuristics that may fail with
// 2/2**32 possibility.  If that become large issues, we need to fix.
// Also, I suppose |len| to Read is usually 2MB size.  Performance
// may suffer if |len| is usually smaller than internal buffer length.
class JarFileReader : public FileReader {
 public:
  ~JarFileReader() override {}

  ssize_t Read(void* ptr, size_t len) override;
  off_t Seek(off_t offset, ScopedFd::Whence whence) const override;
  bool valid() const override { return is_valid_; }
  bool is_recommended() const { return is_recommended_; }
  static void Register() {
    FileReaderFactory::Register(&Create);
  }

 private:
  static std::unique_ptr<FileReader> Create(const std::string& filename);
  static bool CanHandle(const std::string& filename);
  explicit JarFileReader(const std::string& filename);

  ssize_t ReadDataToBuffer(size_t size);
  void NormalizeBuffer();
  ssize_t GetTimestampOffset(const char *signature);

  friend class JarFileReaderTest;
  friend class JarFileNormalizer;
  FRIEND_TEST(JarFileReaderTest, valid);

  // Fields for buffer management.
  std::string buffer_;
  off_t buffer_head_pos_;
  off_t last_normalized_absolute_pos_;
  bool is_buffer_normalized_;
  bool is_central_directory_started_;

  // Fields for user facing part.
  bool is_valid_;
  bool is_recommended_;
  off_t offset_;
  const std::string input_filename_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_JARFILE_READER_H_
