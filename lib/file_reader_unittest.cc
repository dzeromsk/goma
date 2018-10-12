// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/file_reader.h"

#include <memory>

#include "glog/logging.h"
#include "gtest/gtest.h"
using std::string;

namespace devtools_goma {

const int kBufSize = 1024;
const char kDummyValue[] = "dummy value";

class FileReaderFactoryTest : public testing::Test {
};

class FileReaderTest : public testing::Test {
};

class DummyFileReader : public FileReader {
 public:
  // MAGIC number.
  enum { kMagic = 0x55 };

  bool GetFileSize(size_t* file_size) const override {
    *file_size = kMagic;
    return true;
  }

  static std::unique_ptr<FileReader> Create(const string& dummy) {
    called_create_ = true;
    if (create_) {
      is_created_ = true;
      return std::unique_ptr<FileReader>(new DummyFileReader(dummy));
    }
    return nullptr;
  }

  static void Reset(bool create) {
    create_ = create;

    called_create_ = false;
    is_created_ = false;
  }

  // enable / disable the function.
  static bool create_;

  // flags to check the code is executed or not.
  static bool called_create_;
  static bool is_created_;

 private:
  explicit DummyFileReader(const string& filename) : FileReader(filename) {}
};

// enable / disable the function.
bool DummyFileReader::create_;

// flags to check the code is executed or not.
bool DummyFileReader::called_create_;
bool DummyFileReader::is_created_;

TEST_F(FileReaderFactoryTest, Create) {
  std::unique_ptr<FileReader> fr;
  size_t to_verify;
  FileReaderFactory* factory = FileReaderFactory::GetInstance();

  // Nothing registered and get file reader instance.
  fr = factory->NewFileReader("non_existent");
  CHECK(fr);

  FileReaderFactory::Register(&DummyFileReader::Create);
  // Registered class should be selected if subclass Create returns an instance.
  DummyFileReader::Reset(true);
  fr = factory->NewFileReader("non_existent");
  CHECK(fr);
  EXPECT_TRUE(fr->GetFileSize(&to_verify));
  EXPECT_TRUE(DummyFileReader::called_create_);
  EXPECT_TRUE(DummyFileReader::is_created_);
  EXPECT_EQ(DummyFileReader::kMagic, to_verify);

  // Default class should not be used if subclass Create returns nullptr.
  DummyFileReader::Reset(false);
  fr = factory->NewFileReader("non_existent");
  CHECK(fr);
  EXPECT_FALSE(fr->GetFileSize(&to_verify));
  EXPECT_TRUE(DummyFileReader::called_create_);
  EXPECT_FALSE(DummyFileReader::is_created_);
}

TEST_F(FileReaderTest, FlushDataInBuffer) {
  char buf[kBufSize];
  void *ptr;
  size_t len, copied;
  string read_buffer;

  // Should not copy anything if len = 0.
  read_buffer.assign(kDummyValue);
  len = 0;
  buf[0] = '\0';
  ptr = buf;
  copied = FileReader::FlushDataInBuffer(&read_buffer, &ptr, &len);
  EXPECT_EQ(0U, copied);
  EXPECT_EQ(kDummyValue, read_buffer);
  EXPECT_EQ('\0', buf[0]);
  EXPECT_EQ(0U, len);

  // Should copy all data if len > read_buffer_.length().
  read_buffer.assign(kDummyValue);
  len = read_buffer.length() + 1;
  ptr = buf;
  copied = FileReader::FlushDataInBuffer(&read_buffer, &ptr, &len);
  EXPECT_EQ("", read_buffer);
  EXPECT_EQ(kDummyValue, string(buf, copied));
  EXPECT_EQ(1U, len);

  // Should copy all data if len = read_buffer_.length().
  read_buffer.assign(kDummyValue);
  len = read_buffer.length();
  ptr = buf;
  copied = FileReader::FlushDataInBuffer(&read_buffer, &ptr, &len);
  EXPECT_EQ("", read_buffer);
  EXPECT_EQ(strlen(kDummyValue), copied);
  EXPECT_EQ(kDummyValue, string(buf, copied));
  EXPECT_EQ(0U, len);

  // Should remain some data if len < read_buffer_.length().
  read_buffer.assign(kDummyValue);
  len = read_buffer.length() - 1;
  ptr = buf;
  copied = FileReader::FlushDataInBuffer(&read_buffer, &ptr, &len);
  EXPECT_NE("", read_buffer);
  EXPECT_EQ(strlen(kDummyValue) - 1, copied);
  EXPECT_EQ(string(kDummyValue, copied), string(buf, copied));
  EXPECT_EQ(0U, len);
}

}  // namespace devtools_goma
