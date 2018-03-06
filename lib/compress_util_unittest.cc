// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "compress_util.h"

#include <vector>

#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#ifdef ENABLE_LZMA
# include "google/protobuf/io/zero_copy_stream_impl.h"
# include "prototmp/goma_log.pb.h"
using google::protobuf::io::ArrayInputStream;
using google::protobuf::io::ConcatenatingInputStream;
#endif  // ENABLE_LZMA
using std::string;

namespace devtools_goma {

TEST(CompressUtilTest, GetEncodingFromHeader) {
  EXPECT_EQ(ENCODING_DEFLATE, GetEncodingFromHeader("deflate"));
  EXPECT_EQ(ENCODING_LZMA2, GetEncodingFromHeader("lzma2"));
  EXPECT_EQ(ENCODING_LZMA2, GetEncodingFromHeader("deflate,lzma2"));
  EXPECT_EQ(NO_ENCODING, GetEncodingFromHeader(""));
  EXPECT_EQ(NO_ENCODING, GetEncodingFromHeader(nullptr));
}

#ifdef ENABLE_LZMA
// Creates a compressible string.
static string MakeCompressibleTestString() {
  std::ostringstream ss;
  static const int kNumberOfSubStrings = 10000;
  for (int i = 0; i < kNumberOfSubStrings; ++i) {
    ss << i << " ";
  }
  return ss.str();
}

class LZMATest : public testing::Test {
 protected:
  void Compress(const string& input, uint32_t preset, lzma_check check,
                string* output) {
    lzma_stream lzma = LZMA_STREAM_INIT;
    ASSERT_EQ(LZMA_OK, lzma_easy_encoder(&lzma, preset, check));
    ReadAllLZMAStream(input, &lzma, output);
    LOG(INFO) << "Compressed: " << input.size() << " => " << output->size()
              << " with preset=" << preset
              << " check=" << check;
  }

  void Uncompress(const string& input, string* output) {
    lzma_stream lzma = LZMA_STREAM_INIT;
    ASSERT_EQ(LZMA_OK,
              lzma_stream_decoder(&lzma, lzma_easy_decoder_memusage(9), 0));
    ReadAllLZMAStream(input, &lzma, output);
  }

  // Compresses the input string, uncompresses the output, and checks
  // the original string is recovered.
  void RunTest(const string& original_string,
               uint32_t preset, lzma_check check) {
    string compressed_string;
    Compress(original_string, preset, check, &compressed_string);
    string uncompressed_string;
    Uncompress(compressed_string, &uncompressed_string);
    EXPECT_EQ(original_string, uncompressed_string);
  }

  void ConvertToCompressed(const devtools_goma::ExecLog& elog, string* out) {
    string pbuf;
    elog.SerializeToString(&pbuf);
    LOG(INFO) << "orig size=" << pbuf.size();
    Compress(pbuf, 9, LZMA_CHECK_CRC64, out);
  }
};

TEST_F(LZMATest, CompressAndDecompress) {
  RunTest(MakeCompressibleTestString(), 6, LZMA_CHECK_CRC64);
  RunTest(MakeCompressibleTestString(), 9, LZMA_CHECK_NONE);
  RunTest(MakeCompressibleTestString(), 1, LZMA_CHECK_SHA256);
}

TEST_F(LZMATest, LZMAInputStreamTestSimple) {
  devtools_goma::ExecLog elog;
  elog.set_username("goma-user");
  string compressed;
  ConvertToCompressed(elog, &compressed);

  ArrayInputStream input(&compressed[0], compressed.size());
  LZMAInputStream lzma_input(&input);
  devtools_goma::ExecLog alog;
  EXPECT_TRUE(alog.ParseFromZeroCopyStream(&lzma_input));
  EXPECT_EQ(alog.username(), "goma-user");
}

TEST_F(LZMATest, LZMAInputStreamTestChunked) {
  devtools_goma::ExecLog elog;
  elog.set_username("goma-user");
  string compressed;
  ConvertToCompressed(elog, &compressed);

  string former = compressed.substr(0, compressed.size() / 2);
  string latter = compressed.substr(compressed.size() / 2);
  std::vector<ZeroCopyInputStream*> inputs;
  inputs.push_back(new ArrayInputStream(&former[0], former.size()));
  inputs.push_back(new ArrayInputStream(&latter[0], latter.size()));
  ConcatenatingInputStream concatenated(&inputs[0], inputs.size());
  LZMAInputStream lzma_input(&concatenated);
  devtools_goma::ExecLog alog;
  EXPECT_TRUE(alog.ParseFromZeroCopyStream(&lzma_input));
  LOG(INFO) << "lzma_input2. byte count: " << lzma_input.ByteCount();
  EXPECT_EQ(alog.username(), "goma-user");
  for (auto* input : inputs) {
    delete input;
  }
}

#endif

}  // namespace devtools_goma
