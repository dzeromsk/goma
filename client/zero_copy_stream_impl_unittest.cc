// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zero_copy_stream_impl.h"

#include <string>

#include "absl/memory/memory.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#include "http_util.h"

using std::string;

namespace {

string ReadAllFromZeroCopyInputStream(
    google::protobuf::io::ZeroCopyInputStream* input) {
  string data;
  const void* buffer;
  int size;
  while (input->Next(&buffer, &size)) {
    data.append(static_cast<const char*>(buffer), size);
  }
  return data;
}

}  // anonymous namespace

namespace devtools_goma {

TEST(ZeroCopyStreamImplTest, GzipRequestInputStream) {
  constexpr absl::string_view kInputData("input data");

  GzipRequestInputStream::Options options;
  GzipRequestInputStream request(
      absl::make_unique<StringInputStream>(string(kInputData)), options);

  string compressed_req_body = ReadAllFromZeroCopyInputStream(&request);
  EXPECT_TRUE(absl::EndsWith(compressed_req_body, "0\r\n\r\n"))
      << absl::CEscape(compressed_req_body);

  HttpChunkParser parser;
  std::vector<absl::string_view> pieces;
  EXPECT_TRUE(parser.Parse(compressed_req_body, &pieces))
      << parser.error_message()
      << " compressed_data=" << absl::CEscape(compressed_req_body);
  EXPECT_FALSE(pieces.empty());
  EXPECT_TRUE(parser.done());

  std::vector<std::unique_ptr<google::protobuf::io::ZeroCopyInputStream>>
      streams;
  for (auto piece : pieces) {
    streams.push_back(absl::make_unique<google::protobuf::io::ArrayInputStream>(
        piece.data(), piece.size()));
  }

  GzipInputStream gzip_input(
      absl::make_unique<ChainedInputStream>(std::move(streams)));

  string decompressed_data = ReadAllFromZeroCopyInputStream(&gzip_input);
  EXPECT_EQ(kInputData, decompressed_data);
}

}  // namespace devtools_goma
