// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ioutil.h"

#include <memory>
#include <string>

#include "file_helper.h"
#include "google/protobuf/io/gzip_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "gtest/gtest.h"
#include "path.h"
#include "scoped_fd.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

TEST(IoutilTest, GzipInflateWriter) {
  TmpdirUtil tmpdir("ioutil_test_gzip_inflate_writer");
  string filename = file::JoinPath(tmpdir.tmpdir(), "output");
  ScopedFd fd(ScopedFd::Create(filename, 0644));

  std::unique_ptr<WriteCloser> wr(WriteCloser::NewFromScopedFd(std::move(fd)));
  std::unique_ptr<WriteCloser> gwr(WriteCloser::NewGzipInflate(std::move(wr)));

  constexpr absl::string_view kData = "gzip inflate test data";
  string compressed;
  google::protobuf::io::StringOutputStream out(&compressed);
  google::protobuf::io::GzipOutputStream::Options options;
  options.format = google::protobuf::io::GzipOutputStream::GZIP;
  google::protobuf::io::GzipOutputStream gzout(&out, options);
  void* ptr;
  int size;
  EXPECT_TRUE(gzout.Next(&ptr, &size));
  EXPECT_GT(size, kData.size());
  memcpy(ptr, kData.data(), kData.size());
  gzout.BackUp(size - kData.size());
  EXPECT_TRUE(gzout.Close());
  EXPECT_NE(compressed, kData);

  google::protobuf::io::ArrayInputStream in(compressed.data(),
                                            compressed.size());
  const void* buffer;
  while (in.Next(&buffer, &size)) {
    EXPECT_GT(size, 0);
    EXPECT_EQ(size, gwr->Write(buffer, size));
  }
  EXPECT_TRUE(gwr->Close());

  string uncompressed;
  EXPECT_TRUE(ReadFileToString(filename, &uncompressed));
  EXPECT_EQ(kData, uncompressed);
}

}  // namespace devtools_goma
