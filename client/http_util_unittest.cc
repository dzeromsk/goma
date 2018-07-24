// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http_util.h"

#include <gtest/gtest.h>
#include "absl/strings/str_cat.h"
#include "absl/strings/escaping.h"
#include "client/ioutil.h"

using std::string;

namespace devtools_goma {

TEST(HttpUtilTest, FindContentLengthAndBodyOffset) {
  string data = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nH";
  size_t body_offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - 1, body_offset);
  EXPECT_EQ(5UL, content_length);
  EXPECT_FALSE(is_chunked);

  data = "GET / HTTP/1.1\r\nContent-Length: 5\r\n\r\nH";
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - 1, body_offset);
  EXPECT_EQ(5UL, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(HttpUtilTest, FindContentLengthAndBodyOffsetInHeader) {
  string data = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nH";
  size_t body_offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_FALSE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(string::npos, body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  data = "GET / HTTP/1.1\r\nContent-Length: 5\r\nH";
  EXPECT_FALSE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(string::npos, body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(HttpUtilTest, FindContentLengthAndBodyOffsetNoLength) {
  string data = "HTTP/1.1 200 OK\r\nHost: example.com\r\n\r\nH";
  size_t body_offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - 1, body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  data = "HTTP/1.1 200 Ok\r\nHost: example.com\r\n\r\n"
      "Content-Length: 10";
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - strlen("Content-Length: 10"), body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  data = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\nH";
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - 1, body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  data = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"
      "Content-Length: 10";
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - strlen("Content-Length: 10"), body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(HttpUtilTest, FindContentLengthAndBodyOffsetChunked) {
  string data = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1";
  size_t body_offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - 1, body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_TRUE(is_chunked);

  data = "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n1";
  EXPECT_TRUE(FindContentLengthAndBodyOffset(
      data, &content_length, &body_offset, &is_chunked));
  EXPECT_EQ(data.size() - 1, body_offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_TRUE(is_chunked);
}

TEST(HttpUtilTest, ParseHttpResponse) {
  string response = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nHello";
  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(response.size() - strlen("Hello"), offset);
  EXPECT_EQ(5UL, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(HttpUtilTest, ParseHttpResponseInStatusLine) {
  string response = "H";
  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(0, http_status_code);
  response = "HTTP/1.1 ";
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(0, http_status_code);
  response = "HTTP/1.1 200 Ok\r\n";
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);

  response = "HTTP/1.1 204 Ok\r\n";
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(204, http_status_code);
}

TEST(HttpUtilTest, ParseHttpResponseBadStatus) {
  string response = "220 localhost ESMTP";
  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(0, http_status_code);
  EXPECT_EQ(0UL, offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  response = "HTTP/1.1 301 Moved Parmenently\r\n";
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(301, http_status_code);
  EXPECT_EQ(0UL, offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  response = "HTTP/1.1 403 Forbidden\r\n";
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(403, http_status_code);
  EXPECT_EQ(0UL, offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  response = "HTTP/1.1 502 Bad Gateway\r\n";
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(502, http_status_code);
  EXPECT_EQ(0UL, offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(HttpUtilTest, ParseHttpResponseInHeader) {
  string response = "HTTP/1.1 200 Ok\r\nHost: example.com";
  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(0UL, offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  response = "HTTP/1.1 200 Ok\r\nHost: example.com\r\nContent-Length: 5\r\n";
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_FALSE(is_chunked);

  response = "HTTP/1.1 200 Ok\r\nHost: example.com\r\n"
      "Content-Length: 5\r\n\r\n";
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(response.size(), offset);
  EXPECT_EQ(5UL, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(HttpUtilTest, ParseHttpResponseShortBody) {
  string response = "HTTP/1.1 200 Ok\r\nHost: example.com\r\n"
      "Content-Length: 5\r\n\r\nH";
  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(response.size() - 1, offset);
  EXPECT_EQ(5UL, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(HttpUtilTest, ParseHttpResponseChunked) {
  string response = "HTTP/1.1 200 Ok\r\nHost: example.com\r\n"
      "Transfer-Encoding: chunked\r\n\r\n5\r\nhello";
  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(ParseHttpResponse(response, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(response.size() - strlen("5\r\nhello"), offset);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_TRUE(is_chunked);
}

string CombineChunks(const std::vector<absl::string_view>& chunks) {
  string dechunked;
  for (const auto& it : chunks) {
    dechunked.append(it.data(), it.size());
  }
  return dechunked;
}

TEST(HttpUtilTest, ChunkedTransferEncodingWithTwoSpace) {
  static const char* kResponse =
      "HTTP/1.1 200 OK\r\n"
      "Server: Apache\r\n"
      "ETag: \"1d62405a828ad0e52bf86a946ec2113f:1407205214\"\r\n"
      "Last-Modified: Tue, 05 Aug 2014 02:20:14 GMT\r\n"
      "Date: Tue, 05 Aug 2014 02:38:45 GMT\r\n"
      "Transfer-Encoding:  chunked\r\n"
      "Connection: keep-alive\r\n"
      "Connection: Transfer-Encoding\r\n"
      "Content-Type: application/pkix-crl\r\n"
      "\r\n";

  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_TRUE(ParseHttpResponse(kResponse, &http_status_code,
                                &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(string::npos, content_length);
  EXPECT_EQ(true, is_chunked);
}

TEST(HttpUtilTest, ParseQuery) {
  std::map<string, string> params = ParseQuery("");
  EXPECT_TRUE(params.empty());

  static const char* kQuery = "a=b&";
  params = ParseQuery(kQuery);
  EXPECT_EQ(1U, params.size());
  EXPECT_EQ("b", params["a"]);

  static const char* kQueryOAuth2 =
      "state=11882510b1cfd97f015760171d03ec70235880b224fecd15ea1fe490263911d1"
      "&code=4/bfLfMrXvbZ30pYyjloOqCorPiowNEy6Uqeh_oECiGQ8#";
  params = ParseQuery(kQueryOAuth2);
  EXPECT_EQ(2U, params.size());
  EXPECT_EQ("4/bfLfMrXvbZ30pYyjloOqCorPiowNEy6Uqeh_oECiGQ8", params["code"]);
  EXPECT_EQ("11882510b1cfd97f015760171d03ec70235880b224fecd15ea1fe490263911d1",
            params["state"]);
}

void TestHttpChunkParser(absl::string_view response_body,
                         absl::string_view expected_data) {
  SCOPED_TRACE(absl::CEscape(response_body));

  // test on any boundary.
  for (size_t i = 0; i + 1 < response_body.size(); ++i) {
    HttpChunkParser parser;
    std::vector<absl::string_view> pieces;

    absl::string_view body1 = response_body.substr(0, i);
    absl::string_view body2 = response_body.substr(i);
    SCOPED_TRACE(absl::StrCat(
        absl::CEscape(body1), "||", absl::CEscape(body2)));

    EXPECT_TRUE(parser.Parse(body1, &pieces));
    EXPECT_FALSE(parser.done());
    EXPECT_EQ("", parser.error_message());
    SCOPED_TRACE(absl::StrCat("parsed:", absl::CEscape(body1)));
    EXPECT_TRUE(parser.Parse(body2, &pieces));
    EXPECT_TRUE(parser.done());
    EXPECT_EQ("", parser.error_message());
    std::string chunk_data = CombineChunks(pieces);
    EXPECT_EQ(expected_data, chunk_data);
  }

  // test on any steps;
  for (size_t i = 1; i + 1 < response_body.size(); ++i) {
    HttpChunkParser parser;
    std::vector<absl::string_view> pieces;

    absl::string_view stream = response_body;
    SCOPED_TRACE(absl::StrCat("step=", i));
    while (!stream.empty()) {
      absl::string_view in;
      if (i < stream.size()) {
        in = stream.substr(0, i);
        stream.remove_prefix(i);
      } else {
        in = stream;
        stream.remove_prefix(stream.size());
      }
      EXPECT_TRUE(parser.Parse(in, &pieces));
      EXPECT_EQ("", parser.error_message());
      if (parser.done()) {
        EXPECT_TRUE(stream.empty());
        break;
      }
    }
    std::string chunk_data = CombineChunks(pieces);
    EXPECT_EQ(expected_data, chunk_data);
  }

}

TEST(HttpChunkParser, Parse) {
  TestHttpChunkParser("3\r\n"
                      "con\r\n"
                      "8\r\n"
                      "sequence\r\n"
                      "0\r\n"
                      "\r\n",
                      "consequence");
  TestHttpChunkParser("3;n=v\r\n"
                      "con\r\n"
                      "8\r\n"
                      "sequence\r\n"
                      "0\r\n"
                      "\r\n",
                      "consequence");
}

TEST(HttpChunkParser, ParseCrimeMitigation) {
 // CRIME mitigation does followings for obfscating Record Length:
  // 1. Add a particular number of leading zeros to the size string
  // 2. Sub-chunk the body to even smaller chunks
  //
  // See:
  // - go/crime-mitigation-at-gfe-faq
  // - go/crime-mitigation-at-gfe
  TestHttpChunkParser("000004\r\n"
                      "abcd\r\n"
                      "0016\r\n"
                      "efghijklmnopqrstuvwxyz\r\n"
                      "0\r\n"
                      "\r\n",
                      "abcdefghijklmnopqrstuvwxyz");
}

TEST(HttpChunkParser, ParseWikipediaEncodedDataSample) {
  // http://en.wikipedia.org/wiki/Chunked_transfer_encoding#Encoded_data
  TestHttpChunkParser("4\r\n"
                      "Wiki\r\n"
                      "5\r\n"
                      "pedia\r\n"
                      "E\r\n"
                      " in\r\n"
                      "\r\n"
                      "chunks.\r\n"
                      "0\r\n"
                      "\r\n",
                      "Wikipedia in\r\n"
                      "\r\n"
                      "chunks.");
}

TEST(HttpChunkParser, ParseLastChunkExtension) {
  TestHttpChunkParser("3;n=v\r\n"
                      "con\r\n"
                      "8\r\n"
                      "sequence\r\n"
                      "0;n=v\r\n"
                      "\r\n",
                      "consequence");
}

TEST(HttpChunkParser, ParseHexSize) {
  TestHttpChunkParser("3\r\n"
                      "abc\r\n"
                      "0d\r\n"
                      "defghijklmnop\r\n"
                      "a\r\n"
                      "qrstuvwxyz\r\n"
                      "0\r\n\r\n",
                      "abcdefghijklmnopqrstuvwxyz");
  TestHttpChunkParser("1A\r\n"
                      "abcdefghijklmnopqrstuvwxyz\r\n"
                      "0\r\n\r\n",
                      "abcdefghijklmnopqrstuvwxyz");
}

TEST(HttpChunkParser, ParseTrailer) {
  TestHttpChunkParser(
      "3;n=v\r\n"
      "con\r\n"
      "8\r\n"
      "sequence\r\n"
      "0\r\n"
      "X-header: x\r\n"
      "\r\n",
      "consequence");
}

void TestHttpChunkParserErrorInput(absl::string_view response_body) {
  SCOPED_TRACE(response_body);
  // test on any boundary.
  for (size_t i = 1; i + 1 < response_body.size(); ++i) {
    HttpChunkParser parser;
    std::vector<absl::string_view> pieces;

    absl::string_view body1 = response_body.substr(0, i);
    absl::string_view body2 = response_body.substr(i);
    SCOPED_TRACE(absl::StrCat(
        absl::CEscape(body1), "||", absl::CEscape(body2)));

    bool ok = parser.Parse(body1, &pieces);
    if (!ok) {
      EXPECT_FALSE(parser.done());
      EXPECT_NE("", parser.error_message());
      continue;
    }
    SCOPED_TRACE(absl::StrCat("parsed:", absl::CEscape(body1)));
    EXPECT_FALSE(parser.Parse(body2, &pieces));
    EXPECT_FALSE(parser.done());
    EXPECT_NE("", parser.error_message());
  }
}

TEST(HttpChunkParser, ParseError) {
  TestHttpChunkParserErrorInput("3\r\n"
                                "con123\r\n"
                                "seq");
  TestHttpChunkParserErrorInput("3;n=v\r\n"
                                "con128\r\n"
                                "sequence\r\n"
                                "0\r\n"
                                "\r\n");
  TestHttpChunkParserErrorInput("3omg_broken_extension\r\n"
                                "foo\r\n"
                                "0\r\n"
                                "\r\n");
  TestHttpChunkParserErrorInput("-1\r\n"
                                "0\r\n"
                                "\r\n");
  TestHttpChunkParserErrorInput("\r\n\r\n");
}

}  // namespace devtools_goma
