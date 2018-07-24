// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "arfile_reader.h"

#include <list>
#include <string>
#include <vector>

#include "compiler_specific.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"

#ifdef __MACH__
#include "mach_o_parser.h"
#endif

namespace {
#ifdef __MACH__
// dummy filename to create ArFileReader.
static const char kDummyFilename[] = "dummyfilename";
#endif
// dummy value to be used in dummy arfile header and ar entry body.
static const char kDummyValue[] = "dummy value";
// ar_name field of entry header.  This should be 16 bytes.
static const char kDummyArname[] = "dummy           ";
// read buffer size in bytes.
static const size_t kBufSize = 1024;
}  // namespace

namespace devtools_goma {

class StubArFile : public ArFile {
 public:
  StubArFile() : read_header_return_(true) {}
  ~StubArFile() override {}
  bool IsThinArchive() const override { return true; }
  void GetEntries(std::vector<ArFile::EntryHeader>* entries ALLOW_UNUSED)
      override {
    LOG(FATAL) << "Not implemented";
  }
  bool ReadHeader(string* ar_header) const override {
    ar_header->assign(header_);
    return read_header_return_;
  }

  bool ReadEntry(ArFile::EntryHeader* header, string* body) override {
    if (entries_.empty())
      return false;

    EntryInfo info = entries_.front();
    entries_.pop_front();
    *header = info.header;
    body->assign(info.body);
    return info.return_value;
  }

  void SetReadHeaderReturn(bool return_value, const string& header) {
    read_header_return_ = return_value;
    header_.assign(header);
  }

  void AddReadEntryReturn(bool return_value,
                          const ArFile::EntryHeader& header,
                          const string& body) {
    EntryInfo info;
    info.return_value = return_value;
    info.header = header;
    info.body.assign(body);
    entries_.push_back(info);
  }

 private:
  bool read_header_return_;
  string header_;
  struct EntryInfo {
    bool return_value;
    ArFile::EntryHeader header;
    string body;
  };
  std::list<EntryInfo> entries_;
};

TEST(ArFileReaderTest, CanHandle) {
  EXPECT_TRUE(ArFileReader::CanHandle("example.a"));
  EXPECT_FALSE(ArFileReader::CanHandle("example.cc"));
  EXPECT_FALSE(ArFileReader::CanHandle("example.h"));
  EXPECT_FALSE(ArFileReader::CanHandle("example.o"));
  EXPECT_FALSE(ArFileReader::CanHandle("example.a.cc"));
}

TEST(ArFileReaderTest, valid) {
  {
    // Should be invalid if failed to read arfile header.
    std::unique_ptr<StubArFile> arfile(new StubArFile());
    arfile->SetReadHeaderReturn(false, "");
    std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
    EXPECT_FALSE(reader->valid());
  }

  {
    // Should be valid if succeeded to read arfile header.
    std::unique_ptr<StubArFile> arfile(new StubArFile());
    arfile->SetReadHeaderReturn(true, kDummyValue);
    std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
    EXPECT_TRUE(reader->valid());
    EXPECT_EQ(kDummyValue, reader->read_buffer_);
  }
}

TEST(ArFileReaderTest, NormalizeArHeader) {
  // ar header should be normalized.
  ArFile::EntryHeader hdr;
  hdr.ar_name.assign(kDummyValue);
  hdr.orig_ar_name.assign(kDummyValue);
  hdr.ar_date = 1;
  hdr.ar_uid = 1;
  hdr.ar_gid = 1;
  hdr.ar_mode = 1;
  hdr.ar_size = 1;

  std::unique_ptr<StubArFile> arfile(new StubArFile());
  std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
  reader->NormalizeArHdr(&hdr);
  EXPECT_EQ(kDummyValue, hdr.ar_name);
  EXPECT_EQ(kDummyValue, hdr.orig_ar_name);
  EXPECT_EQ(0L, hdr.ar_date);
  EXPECT_EQ(0U, hdr.ar_uid);
  EXPECT_EQ(0U, hdr.ar_gid);
  EXPECT_EQ(0U, hdr.ar_mode);
  EXPECT_EQ(1U, hdr.ar_size);
}

TEST(ArFileReaderTest, Read) {
  char buf[kBufSize];
  ssize_t len, copied;
  ArFile::EntryHeader dummy_entry_header, expected_entry_header;
  dummy_entry_header.ar_name.assign(kDummyArname);
  dummy_entry_header.orig_ar_name.assign(kDummyArname);
  dummy_entry_header.ar_date = 0xaa;
  dummy_entry_header.ar_uid = 0xaa;
  dummy_entry_header.ar_gid = 0xaa;
  dummy_entry_header.ar_mode = 0xaa;
  dummy_entry_header.ar_size = strlen(kDummyValue);
  expected_entry_header.ar_name.assign(kDummyArname);
  expected_entry_header.orig_ar_name.assign(kDummyArname);
  expected_entry_header.ar_date = 0;
  expected_entry_header.ar_uid = 0;
  expected_entry_header.ar_gid = 0;
  expected_entry_header.ar_mode = 0;
  expected_entry_header.ar_size = strlen(kDummyValue);
  string entry_header_string;
  CHECK(expected_entry_header.SerializeToString(&entry_header_string));
  string expected_out;

  {
    // 0. Should not read anything if len = 0.
    std::unique_ptr<StubArFile> arfile(new StubArFile());
    arfile->SetReadHeaderReturn(true, kDummyValue);
    std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
    CHECK(reader);
    len = 0;
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(0, copied);
    EXPECT_EQ(kDummyValue, reader->read_buffer_);
    EXPECT_EQ('\0', buf[0]);
  }

  {
    // 1. Should read just header file if buffer size is kDummyValue length.
    std::unique_ptr<StubArFile> arfile(new StubArFile());
    arfile->SetReadHeaderReturn(true, kDummyValue);
    std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
    CHECK(reader);
    len = strlen(kDummyValue);
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ("", reader->read_buffer_);
    EXPECT_EQ(kDummyValue, string(buf, copied));
  }

  {
    // 2. Should refill if len > header length.
    std::unique_ptr<StubArFile> arfile(new StubArFile());
    arfile->SetReadHeaderReturn(true, kDummyValue);
    arfile->AddReadEntryReturn(true, dummy_entry_header, kDummyValue);
    expected_out.assign(kDummyValue);
    expected_out.append(entry_header_string);
    expected_out.append(kDummyValue);
    std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
    CHECK(reader);
    len = expected_out.size();
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ("", reader->read_buffer_);
    EXPECT_EQ(expected_out, string(buf, copied));
  }

  {
    // 3. Should allow remaining data if len < size and able to read remaining.
    // +---------------+
    // + arfile header | <- 3-1
    // +---------------+
    // | entry header  | <- 3-2
    // +-.-.-.-.-.-.-.-+ <- 3-3
    // | entry body    | <- 3-4
    // +---------------+ <- 3-5.
    // | entry header 2|
    // +-.-.-.-.-.-.-.-+
    // | entry body 2  |
    // +---------------+ <- 3-6.
    std::unique_ptr<StubArFile> arfile(new StubArFile());
    arfile->SetReadHeaderReturn(true, kDummyValue);
    arfile->AddReadEntryReturn(true, dummy_entry_header, kDummyValue);
    arfile->AddReadEntryReturn(true, dummy_entry_header, kDummyValue);
    std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
    CHECK(reader);

    // 3-1. len is middle of the arfile header.
    expected_out.assign(kDummyValue);
    expected_out = expected_out.substr(0, expected_out.size() / 2);
    len = expected_out.size();
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ(expected_out, string(buf, copied));

    // 3-2. len is middle of the entry header.
    expected_out.assign(kDummyValue);
    expected_out = expected_out.substr(expected_out.size() / 2);
    expected_out.append(
        entry_header_string.substr(0, entry_header_string.size() / 2));
    len = expected_out.size();
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ(expected_out, string(buf, copied));

    // 3-3. len is end of the entry header.
    expected_out.assign(
        entry_header_string.substr(entry_header_string.size() / 2));
    len = expected_out.size();
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ(expected_out, string(buf, copied));

    // 3-4. len is middle of the entry body.
    expected_out.assign(kDummyValue);
    expected_out = expected_out.substr(0, expected_out.size() / 2);
    len = expected_out.size();
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ(expected_out, string(buf, copied));

    // 3-5 read the remaining data.
    expected_out.assign(kDummyValue);
    expected_out = expected_out.substr(expected_out.size() / 2);
    len = expected_out.size();
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ("", reader->read_buffer_);
    EXPECT_EQ(expected_out, string(buf, copied));

    // 3-6 read the remaining data.
    expected_out.assign(entry_header_string);
    expected_out.append(kDummyValue);
    len = expected_out.size();
    buf[0] = '\0';
    copied = reader->Read(buf, len);
    EXPECT_EQ(len, copied);
    EXPECT_EQ("", reader->read_buffer_);
    EXPECT_EQ(expected_out, string(buf, copied));
  }

  {
    // 4. Should return -1 if ReadEntry failed.
    std::unique_ptr<StubArFile> arfile(new StubArFile());
    arfile->SetReadHeaderReturn(true, kDummyValue);
    std::unique_ptr<ArFileReader> reader(new ArFileReader(std::move(arfile)));
    CHECK(reader);
    len = strlen(kDummyValue) + 1;
    EXPECT_EQ(-1, reader->Read(buf, len));
  }
}

#ifdef __MACH__
class StubArFileReader : public ArFileReader {
 public:
  explicit StubArFileReader(const string& filename)
      : ArFileReader(filename), valid_(true) {}

  bool valid() const override { return valid_; }
  ssize_t Read(void* ptr, size_t len) override {
    FileReader::FlushDataInBuffer(&contents_, &ptr, &len);
    return read_return_;
  }

  void SetValid(bool valid) {
    valid_ = valid;
  }

  void SetReadReturn(ssize_t return_value, const string contents) {
    read_return_ = return_value;
    contents_.assign(contents);
  }

 private:
  bool valid_;
  ssize_t read_return_;
  string contents_;
};


class FatArFileReaderTest : public FatArFileReader::ArFileReaderFactory,
                            public testing::Test {
 protected:
  // Takes ownership of |fhdr|.
  std::unique_ptr<FatArFileReader> CreateFatArFileReader(
      std::unique_ptr<MacFatHeader> fhdr) {
    return std::unique_ptr<FatArFileReader>(new FatArFileReader(
        kDummyFilename, std::move(fhdr), this));
  }

  // Takes ownership of |fhdr|.
  std::unique_ptr<FatArFileReader> CreateValidArFileReader(
      std::unique_ptr<MacFatHeader> fhdr) {
    CHECK(fhdr);
    CHECK_GT(fhdr->archs.size(), static_cast<size_t>(0));
    CHECK_GT(fhdr->archs[0].size, static_cast<size_t>(0));

    std::unique_ptr<StubArFileReader> stub_reader(
        new StubArFileReader(kDummyFilename));
    stub_reader->SetValid(true);
    stub_reader->SetReadReturn(fhdr->archs[0].size, kDummyValue);
    arfile_reader_.push_back(std::move(stub_reader));
    std::unique_ptr<FatArFileReader> fat_reader(
        CreateFatArFileReader(std::move(fhdr)));
    EXPECT_TRUE(fat_reader->valid());
    return fat_reader;
  }

  // Makes dummy ArFileReader.
  std::unique_ptr<ArFileReader> CreateArFileReader(
      const string& filename, off_t offset) override {
    CHECK(!arfile_reader_.empty());
    std::unique_ptr<ArFileReader> ret(std::move(arfile_reader_.front()));
    arfile_reader_.pop_front();
    return ret;
  }

  std::list<std::unique_ptr<ArFileReader>> arfile_reader_;
};

TEST_F(FatArFileReaderTest, valid) {
  std::unique_ptr<FatArFileReader> fat_reader;
  std::unique_ptr<MacFatHeader> f_hdr;
  MacFatArch dummy_arch;
  std::unique_ptr<StubArFileReader> stub_reader;

  // Should be invalid if arfile_reader is invalid.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  f_hdr->archs.push_back(dummy_arch);

  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(false);
  arfile_reader_.push_back(std::move(stub_reader));
  fat_reader = CreateFatArFileReader(std::move(f_hdr));
  EXPECT_FALSE(fat_reader->valid());
  EXPECT_TRUE(arfile_reader_.empty());

  // Should be invalid if arfile_reader Read failed.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(-1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));
  fat_reader = CreateFatArFileReader(std::move(f_hdr));
  EXPECT_FALSE(fat_reader->valid());
  EXPECT_TRUE(arfile_reader_.empty());

  // Should be invalid if arfile_reader Read size small.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 3;
  f_hdr->archs.push_back(dummy_arch);

  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));
  fat_reader = CreateFatArFileReader(std::move(f_hdr));
  EXPECT_FALSE(fat_reader->valid());
  EXPECT_TRUE(arfile_reader_.empty());

  // Should be valid if arfile_reader Read success.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));
  fat_reader = CreateFatArFileReader(std::move(f_hdr));
  EXPECT_TRUE(fat_reader->valid());
  EXPECT_TRUE(arfile_reader_.empty());
}


TEST_F(FatArFileReaderTest, Read) {
  std::unique_ptr<FatArFileReader> fat;
  std::unique_ptr<MacFatHeader> f_hdr;
  std::unique_ptr<StubArFileReader> stub_reader;
  MacFatArch dummy_arch;
  char buf[kBufSize];
  ssize_t len, copied;

  // 0. Returns -1 if invalid.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  fat = CreateValidArFileReader(std::move(f_hdr));
  len = fat->read_buffer_.size();
  fat->is_valid_ = false;
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, static_cast<ssize_t>(-1));
  EXPECT_TRUE(arfile_reader_.empty());

  // 1. able to fill data only from read_buffer_.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  fat =  CreateValidArFileReader(std::move(f_hdr));
  len = fat->read_buffer_.size();
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, len);
  EXPECT_TRUE(arfile_reader_.empty());

  // 2. refill and can return data.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  dummy_arch.size = sizeof(kDummyValue) - 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(sizeof(kDummyValue) - 1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));

  len = fat->read_buffer_.size() + 1;
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, len);
  EXPECT_TRUE(arfile_reader_.empty());

  // 3. 2 times refill and can return data.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));
  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));

  len = fat->read_buffer_.size() + 2;
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, len);
  EXPECT_TRUE(arfile_reader_.empty());

  // 4-1. try to refill but no more archs.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  len = fat->read_buffer_.size() + 2;
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, len - 2);
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, static_cast<ssize_t>(-1));
  EXPECT_TRUE(arfile_reader_.empty());

  // 4-2. try to refill but no more archs with empty read buffer.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  fat->read_buffer_.clear();
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, static_cast<ssize_t>(-1));
  EXPECT_TRUE(arfile_reader_.empty());

  // 5-1. try to refill but got invalid arfile reader.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(false);
  stub_reader->SetReadReturn(1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));

  len = fat->read_buffer_.size() + 2;
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, len - 2);
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, static_cast<ssize_t>(-1));
  EXPECT_TRUE(arfile_reader_.empty());

  // 5-2. try to refill but got invalid arfile reader with emtpy read buffer.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(false);
  stub_reader->SetReadReturn(1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));

  fat->read_buffer_.clear();
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, static_cast<ssize_t>(-1));
  EXPECT_TRUE(arfile_reader_.empty());

  // 6-1. try to refill but failed to read.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(-1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));

  len = fat->read_buffer_.size() + 2;
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, len - 2);
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, static_cast<ssize_t>(-1));
  EXPECT_TRUE(arfile_reader_.empty());

  // 6-2. try to refill but failed to read with emtpy read buffer.
  arfile_reader_.clear();
  f_hdr.reset(new MacFatHeader);
  f_hdr->raw.assign(kDummyValue);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);
  dummy_arch.size = 1;
  f_hdr->archs.push_back(dummy_arch);

  fat = CreateValidArFileReader(std::move(f_hdr));
  stub_reader.reset(new StubArFileReader(kDummyFilename));
  stub_reader->SetValid(true);
  stub_reader->SetReadReturn(-1, kDummyValue);
  arfile_reader_.push_back(std::move(stub_reader));

  fat->read_buffer_.clear();
  copied = fat->Read(buf, len);
  EXPECT_EQ(copied, static_cast<ssize_t>(-1));
  EXPECT_TRUE(arfile_reader_.empty());
}

#endif

}  // namespace devtools_goma
