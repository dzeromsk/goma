// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// FileReader subclass that normalize ar file during reading time.
//
// Even if you create ar file from same object files, The created files
// are different. That is because ar file contains information that
// comes from file stat's. For the better cache hit, we want the same ar file
// for the same objects. ArFileReader normalize it during reading.
// The class is thread-unsafe.

#ifndef DEVTOOLS_GOMA_CLIENT_ARFILE_READER_H_
#define DEVTOOLS_GOMA_CLIENT_ARFILE_READER_H_

#include <memory>
#include <string>

#include "arfile.h"
#ifdef _WIN32
#include "config_win.h"
#endif
#include "file_reader.h"
#include "gtest/gtest_prod.h"
#include "scoped_fd.h"

namespace devtools_goma {

struct MacFatHeader;
struct MacFatArch;

// A subclass of FileReader to normalize ar file during reading.
class ArFileReader : public FileReader {
 public:
  ssize_t Read(void* ptr, size_t len) override;
  off_t Seek(off_t offset, ScopedFd::Whence whence) const override;
  bool valid() const override { return is_valid_; }
  static void Register() {
    FileReaderFactory::Register(&Create);
  }

 private:
  // Returns an instance of ArFileReader if this class can handle |filename|.
  // Otherwise, returns nullptr.
  static std::unique_ptr<FileReader> Create(const std::string& filename);
  // Returns true if |filename| is .a file's name.
  static bool CanHandle(const std::string& filename);
  // Takes ownership of |arfile|.
  explicit ArFileReader(std::unique_ptr<ArFile> arfile);

  // DON'T USE THIS.
  // This is only provided for the test.
  explicit ArFileReader(const std::string& filename) : FileReader(filename) {}

  // Normalizes |hdr|.
  // Keeps fields that won't change for the same object files.  Clears anything
  // else.
  static void NormalizeArHdr(ArFile::EntryHeader* hdr);

  off_t current_offset_;
  // Data to be copied by Read function is stored to |read_buffer_|.
  // If |len| of Read function is less than |read_buffer_|, remained data will
  // be kept here until next call of Read.
  std::string read_buffer_;
  std::unique_ptr<ArFile> arfile_;
  bool is_valid_;

  friend class FatArFileReader;
  friend class ArFileReaderTest;
  friend class StubArFileReader;
  FRIEND_TEST(ArFileReaderTest, Read);
  FRIEND_TEST(ArFileReaderTest, valid);
  FRIEND_TEST(ArFileReaderTest, CanHandle);
  FRIEND_TEST(ArFileReaderTest, NormalizeArHeader);
  DISALLOW_COPY_AND_ASSIGN(ArFileReader);
};

#ifdef __MACH__
class FatArFileReader : public FileReader {
 public:
  ssize_t Read(void* ptr, size_t len) override;
  off_t Seek(off_t offset, ScopedFd::Whence whence) const override;
  bool valid() const override { return is_valid_; }

 private:
  class ArFileReaderFactory {
   public:
    virtual std::unique_ptr<ArFileReader> CreateArFileReader(
        const std::string& filename, off_t offset) = 0;
  };

  // Take ownership of |f_hdr|.
  FatArFileReader(const std::string& filename,
                  std::unique_ptr<MacFatHeader> f_hdr);

  // Register creators to get ArFile and ArFileReader instance.
  // This is only provided for test.
  // Does not take ownership of |create_arfile_reader|.
  FatArFileReader(const std::string& filename,
                  std::unique_ptr<MacFatHeader> f_hdr,
                  ArFileReaderFactory* create_arfile_reader);

  std::unique_ptr<ArFileReader> CreateArFileReader(const std::string& filename,
                                                   off_t offset);

  void Init();
  ssize_t ReturnReadError(ssize_t read_bytes);

  friend class ArFileReader;
  friend class FatArFileReaderTest;
  FRIEND_TEST(FatArFileReaderTest, Read);
  bool is_valid_;
  std::string filename_;
  std::unique_ptr<MacFatHeader> f_hdr_;
  std::unique_ptr<ArFileReader> arr_;
  std::string read_buffer_;
  off_t current_offset_;

  // To point architecture-related arfile position.
  MacFatArch* cur_arch_;
  size_t cur_arch_idx_;

  ArFileReaderFactory* create_arfile_reader_factory_;
  DISALLOW_COPY_AND_ASSIGN(FatArFileReader);
};
#endif

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_ARFILE_READER_H_
