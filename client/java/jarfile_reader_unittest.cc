// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "jarfile_reader.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "file_helper.h"
#include "filesystem.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
#include "gtest/gtest.h"
#include "mypath.h"
#include "path.h"
#include "unittest_util.h"

// How to make jar file to be used as expected.jar.
// 1. create .jar file.
// 2. execute jarfile_normalizer to create the normalized jar file.
// 3. use test/verify_normalized_jar.py to verify normalized jar file.

// How to make jar file to be used as ziptime.jar.
// 1. create .jar file.
// 2. execute ziptime in android prebuilts.
// e.g. prebuilts/build-tools/linux-x86/bin/ziptime

namespace devtools_goma {

class JarFileReaderTest : public testing::Test {
 public:
  void SetUp() override {
    tmpdir_util_ = absl::make_unique<TmpdirUtil>("jar_parser_unittest");
    tmpdir_util_->SetCwd("");
  }

 protected:
  std::string CopyArchiveIntoTestDir(const std::string& test_name,
                                     const std::string& archive) {
    // This module is out\Release\jarfile_reader_unittest.
    const std::string parent_dir = file::JoinPath(GetMyDirectory(), "..");
    const std::string top_dir = file::JoinPath(parent_dir, "..");
    const std::string test_dir = file::JoinPath(top_dir, "test");
    const std::string source_file =
        file::JoinPath(test_dir, test_name + ".jar");
    const std::string output_file = tmpdir_util_->FullPath(archive);
    CHECK(file::Copy(source_file, output_file, file::Defaults()).ok());
    return output_file;
  }

  void EnsureDifferentFiles(const std::string& file1,
                            const std::string& file2) {
    std::string content1, content2;
    ASSERT_TRUE(ReadFileToString(file1, &content1));
    ASSERT_TRUE(ReadFileToString(file2, &content2));
    ASSERT_NE(content1, content2);
  }

  void RunTest(const std::string& expected_file,
               const std::string& orig_file,
               size_t buf_size) {
    EnsureDifferentFiles(expected_file, orig_file);

    ScopedFd fd(ScopedFd::OpenForRead(expected_file));
    ASSERT_TRUE(fd.valid());

    JarFileReader reader(orig_file);
    ASSERT_TRUE(reader.valid());

    off_t offset = 0;
    for (int cnt = 0;; ++cnt) {
      VLOG(1) << "reading: " << cnt * buf_size << " to "
              << (cnt + 1) * buf_size;
      std::unique_ptr<char[]> jar_buf(new char[buf_size]);
      std::unique_ptr<char[]> expected_buf(new char[buf_size]);
      EXPECT_EQ(offset, reader.Seek(offset, ScopedFd::SeekAbsolute));
      ssize_t read_bytes = reader.Read(jar_buf.get(), buf_size);
      if (read_bytes <= 0) {
        EXPECT_TRUE(fd.Read(expected_buf.get(), buf_size) <= 0);
        break;
      }
      offset += read_bytes;

      ASSERT_EQ(read_bytes, fd.Read(expected_buf.get(), read_bytes));
      EXPECT_EQ(absl::string_view(expected_buf.get(), read_bytes),
                absl::string_view(jar_buf.get(), read_bytes));
    }
  }

  void ReadFile(const std::string& jar_file) {
    JarFileReader reader(jar_file);
    ASSERT_TRUE(reader.valid());
    for (;;) {
      char buf[4096];
      ssize_t read_bytes = reader.Read(buf, sizeof(buf));
      if (read_bytes <= 0) {
        break;
      }
    }
  }

  bool CanHandle(const std::string& filename) const {
    return JarFileReader::CanHandle(filename);
  }

  bool IsValid(const std::string& filename) const {
    JarFileReader reader(filename);
    return reader.valid();
  }

  bool DetectedZipNormalizedTime(const std::string& filename) const {
    JarFileReader reader(filename);
    return reader.detected_zip_normalized_time();
  }

  std::unique_ptr<TmpdirUtil> tmpdir_util_;
};

TEST_F(JarFileReaderTest, valid) {
  const std::string jar = CopyArchiveIntoTestDir("Basic", "foo.jar");

  JarFileReader reader(jar);
  EXPECT_TRUE(reader.valid());
}

TEST_F(JarFileReaderTest, ConfirmItNormalizedBasic) {
  const std::string jar_original =
      CopyArchiveIntoTestDir("Basic", "original.jar");
  const std::string jar_expected =
      CopyArchiveIntoTestDir("Basic_expected", "expected.jar");

  RunTest(jar_expected, jar_original, 32);
}

TEST_F(JarFileReaderTest, ConfirmItNormalizedComplicated) {
  const std::string jar_original =
      CopyArchiveIntoTestDir("signapk", "original.jar");
  const std::string jar_expected =
      CopyArchiveIntoTestDir("signapk_expected", "expected.jar");

  for (size_t i = 8; i < 22; ++i) {
    size_t buf_size = std::pow(2, i);
    LOG(INFO) << "buf_size=" << buf_size;
    RunTest(jar_expected, jar_original, buf_size);
  }
}

#if GTEST_HAS_DEATH_TEST && DCHECK_IS_ON()
TEST_F(JarFileReaderTest, ShouldDieIfLocalFileComesAfterCentralDirectory) {
  const std::string jar_broken = CopyArchiveIntoTestDir("Broken", "broken.jar");

  EXPECT_DEATH(ReadFile(jar_broken), "");
}

#endif // GTEST_HAS_DEATH_TEST && DCHECK_IS_ON()

TEST_F(JarFileReaderTest, CanHandle) {
  EXPECT_TRUE(CanHandle("/home/foo/test.jar"));
  EXPECT_FALSE(CanHandle("/home/foo/test.txt"));
}

TEST_F(JarFileReaderTest, Valid) {
  const std::string jar_original =
      CopyArchiveIntoTestDir("signapk", "original.jar");
  const std::string jar_ziptime =
      CopyArchiveIntoTestDir("signapk_ziptime", "ziptime.jar");
  // Note: asm.jar does not have valid jar file magic.
  const std::string jar_asm = CopyArchiveIntoTestDir("asm", "asm.jar");

  EXPECT_TRUE(IsValid(jar_original));
  EXPECT_TRUE(IsValid(jar_ziptime));
  EXPECT_TRUE(IsValid(jar_asm));
}

TEST_F(JarFileReaderTest, DetectedZipNormalizedTime) {
  const std::string jar_original =
      CopyArchiveIntoTestDir("signapk", "original.jar");
  const std::string jar_ziptime =
      CopyArchiveIntoTestDir("signapk_ziptime", "ziptime.jar");

  EXPECT_FALSE(DetectedZipNormalizedTime(jar_original));
  EXPECT_TRUE(DetectedZipNormalizedTime(jar_ziptime));
}

}  // namespace devtools_goma
