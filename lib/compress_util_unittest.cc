// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "compress_util.h"

#include <memory>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "gtest/gtest.h"
#ifdef ENABLE_LZMA
# include "google/protobuf/io/zero_copy_stream_impl.h"
# include "prototmp/goma_log.pb.h"
using google::protobuf::io::ArrayInputStream;
using google::protobuf::io::ConcatenatingInputStream;
using google::protobuf::io::StringOutputStream;
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

// Helper function for compression test.
static bool ReadAllLZMAStream(absl::string_view input, lzma_stream* lzma,
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

  void ConvertToUncompressed(const string& in, devtools_goma::ExecLog* elog) {
    string pbuf;
    Uncompress(in, &pbuf);
    elog->ParseFromString(pbuf);
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

  LZMAInputStream lzma_input(
      absl::make_unique<ArrayInputStream>(&compressed[0], compressed.size()));
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
  LZMAInputStream lzma_input(
      absl::make_unique<ConcatenatingInputStream>(&inputs[0], inputs.size()));
  devtools_goma::ExecLog alog;
  EXPECT_TRUE(alog.ParseFromZeroCopyStream(&lzma_input));
  LOG(INFO) << "lzma_input2. byte count: " << lzma_input.ByteCount();
  EXPECT_EQ(alog.username(), "goma-user");
  for (auto* input : inputs) {
    delete input;
  }
}

TEST_F(LZMATest, LZMAOutputStreamTestSimple) {
  devtools_goma::ExecLog elog;
  elog.set_username("goma-user");
  string compressed;
  LZMAOutputStream lzstream(absl::make_unique<StringOutputStream>(&compressed));
  elog.SerializeToZeroCopyStream(&lzstream);
  EXPECT_TRUE(lzstream.Close());

  devtools_goma::ExecLog alog;
  ConvertToUncompressed(compressed, &alog);
  EXPECT_EQ(alog.username(), "goma-user");
}

TEST_F(LZMATest, LZMAOutputStreamTestWithOption) {
  devtools_goma::ExecLog elog;
  elog.set_username("goma-user");
  string compressed;
  LZMAOutputStream::Options options;
  options.preset = 1;
  options.check = LZMA_CHECK_NONE;
  options.buffer_size = 1;
  LZMAOutputStream lzstream(absl::make_unique<StringOutputStream>(&compressed));
  elog.SerializeToZeroCopyStream(&lzstream);
  EXPECT_TRUE(lzstream.Close());

  devtools_goma::ExecLog alog;
  ConvertToUncompressed(compressed, &alog);
  EXPECT_EQ(alog.username(), "goma-user");
}

TEST_F(LZMATest, LZMAStreamEndToEnd) {
  devtools_goma::ExecLog elog;
  elog.set_username("goma-user");
  string compressed;
  LZMAOutputStream lzstream(absl::make_unique<StringOutputStream>(&compressed));
  elog.SerializeToZeroCopyStream(&lzstream);
  EXPECT_TRUE(lzstream.Close());

  LZMAInputStream lzma_input(
      absl::make_unique<ArrayInputStream>(&compressed[0], compressed.size()));
  devtools_goma::ExecLog alog;
  EXPECT_TRUE(alog.ParseFromZeroCopyStream(&lzma_input));
  EXPECT_EQ(alog.username(), "goma-user");
}

#endif

}  // namespace devtools_goma
