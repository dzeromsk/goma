// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "ioutil.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "unittest_util.h"

using std::string;

namespace devtools_goma {

#if GTEST_HAS_DEATH_TEST
TEST(IoutilTest, WriteStringToFileOrDieCrash) {
#ifndef _WIN32
  string not_exists = "/tmp/you_may_not_have_this_dir/foo/bar/baz";
  EXPECT_DEATH(WriteStringToFileOrDie("fuga", not_exists, 0666),
               "No such file");
#else
  string not_exists = "K:\\tmp\\you_may_not_have_this_dir\\foo\\bar\\baz";
  EXPECT_DEATH(WriteStringToFileOrDie("fuga", not_exists, 0666), "");
#endif
}
#endif  // GTEST_HAS_DEATH_TEST

#ifdef _WIN32
TEST(IoutilTest, DeleteRecursivelyOrDieCrash) {
  char tmp_dir[PATH_MAX], first_dir[PATH_MAX];
  GetTempPathA(PATH_MAX, tmp_dir);
  if (tmp_dir[strlen(tmp_dir) - 1] == '\\') {
    tmp_dir[strlen(tmp_dir) - 1] = 0;
  }
  sprintf_s(first_dir, PATH_MAX, "%s\\ioutils_unittest_%d",
            tmp_dir, GetCurrentProcessId());
  CreateDirectoryA(first_dir, nullptr);
  string second_dir = first_dir;
  second_dir += "\\foo";
  CreateDirectoryA(second_dir.c_str(), nullptr);
  string file = second_dir;
  file += "\\something.txt";
  FILE* fp = nullptr;
  EXPECT_EQ(0, fopen_s(&fp, file.c_str(), "w"));
  EXPECT_TRUE(fp != nullptr);
  fputs("bar", fp);
  fflush(fp);
  fclose(fp);
  // Shall not die here
  DeleteRecursivelyOrDie(first_dir);
  // Shall die here
  EXPECT_DEATH(DeleteRecursivelyOrDie(first_dir), "");
}
#endif

#ifndef _WIN32
TEST(IoutilTest, GetCurrentDirNameOrDie) {
  // NOTE: '1' in setenv mean overwrite.

  std::unique_ptr<char, decltype(&free)> original_env_pwd(nullptr, free);
  std::unique_ptr<char, decltype(&free)> original_cwd(nullptr, free);
  {
    const char* pwd = getenv("PWD");
    if (pwd != nullptr) {
      original_env_pwd.reset(strdup(pwd));
    }

    // Assuming we can obtain the resolved absolute cwd.
    original_cwd.reset(getcwd(nullptr, 0));
    ASSERT_NE(original_cwd.get(), nullptr);
  }

  // When PWD is invalid place, it should not be used.
  {
    ASSERT_EQ(setenv("PWD", "/somewhere/invalid/place", 1), 0);
    std::string cwd = GetCurrentDirNameOrDie();
    EXPECT_NE("/somewhere/invalid/place", cwd);
    // should be the same as getcwd.
    EXPECT_EQ(original_cwd.get(), cwd);
  }

  // When PWD is /proc/self/cwd, it should not be used.
  // Since the meaning of /proc/self/cwd is different among gomacc and
  // compiler_proxy, we should not use /proc/self/cwd.
  {
    ASSERT_EQ(setenv("PWD", "/proc/self/cwd", 1), 0);
    std::string cwd = GetCurrentDirNameOrDie();
    EXPECT_NE("/proc/self/cwd", cwd);
    // should be the same as getcwd.
    EXPECT_EQ(original_cwd.get(), cwd);
  }

  {
    TmpdirUtil tmpdir("ioutil_tmpdir");
    // TODO: TmpdirUtil does not make cwd. why?
    tmpdir.MkdirForPath(tmpdir.cwd(), true);

    // Make a symlink $tmpdir_cwd/cwd --> real cwd.
    std::string newpath = tmpdir.FullPath("cwd");
    ASSERT_EQ(symlink(original_cwd.get(), newpath.c_str()), 0)
        << "from=" << newpath << " to=" << original_cwd.get();
    ASSERT_NE(original_cwd.get(), newpath);

    // set PWD as new path. Then the new path should be taken.
    setenv("PWD", newpath.c_str(), 1);
    std::string cwd = GetCurrentDirNameOrDie();
    EXPECT_EQ(cwd, newpath);

    // Need to unlink symlink. Otherwise. TmpdirUtil will recursively delete
    // the current working directory. Awful (>x<).
    ASSERT_EQ(unlink(newpath.c_str()), 0);
  }

  // ----- tear down the test for the safe.
  if (original_env_pwd) {
    setenv("PWD", original_cwd.get(), 1);
  } else {
    unsetenv("PWD");
  }
}
#endif

TEST(IoutilTest, FindContentLengthAndBodyOffset) {
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

TEST(IoutilTest, FindContentLengthAndBodyOffsetInHeader) {
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

TEST(IoutilTest, FindContentLengthAndBodyOffsetNoLength) {
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

TEST(IoutilTest, FindContentLengthAndBodyOffsetChunked) {
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

TEST(IoutilTest, ParseHttpResponse) {
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

TEST(IoutilTest, ParseHttpResponseInStatusLine) {
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

TEST(IoutilTest, ParseHttpResponseBadStatus) {
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

TEST(IoutilTest, ParseHttpResponseInHeader) {
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
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(response.size(), offset);
  EXPECT_EQ(5UL, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(IoutilTest, ParseHttpResponseShortBody) {
  string response = "HTTP/1.1 200 Ok\r\nHost: example.com\r\n"
      "Content-Length: 5\r\n\r\nH";
  int http_status_code = 0;
  size_t offset = string::npos;
  size_t content_length = string::npos;
  bool is_chunked = false;
  EXPECT_FALSE(ParseHttpResponse(response, &http_status_code,
                                 &offset, &content_length, &is_chunked));
  EXPECT_EQ(200, http_status_code);
  EXPECT_EQ(response.size() - 1, offset);
  EXPECT_EQ(5UL, content_length);
  EXPECT_FALSE(is_chunked);
}

TEST(IoutilTest, ParseHttpResponseChunked) {
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

TEST(IoutilTest, ParseChunkedBodyShouldParse) {
  // HTTP header is dummy.
  static const char* kResponse =
      "Dummy\r\n\r\n3\r\ncon\r\n8\r\nsequence\r\n0\r\n\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining = string::npos;

  EXPECT_TRUE(ParseChunkedBody(kResponse,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(0U, remaining);
  EXPECT_EQ(2U, chunks.size());
  const string dechunked = CombineChunks(chunks);
  EXPECT_EQ(11U, dechunked.size());
  EXPECT_EQ("consequence", dechunked);
}

TEST(IoutilTest, ParseChunkedBodyShouldSkipChunkExtension) {
  // HTTP header is dummy.
  static const char* kResponse =
      "Dummy\r\n\r\n3;n=v\r\ncon\r\n8\r\nsequence\r\n0\r\n\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining = string::npos;

  EXPECT_TRUE(ParseChunkedBody(kResponse,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(0U, remaining);
  EXPECT_EQ(2U, chunks.size());
  const string dechunked = CombineChunks(chunks);
  EXPECT_EQ(11U, dechunked.size());
  EXPECT_EQ("consequence", dechunked);
}

TEST(IoutilTest, ParseChunkedBodyShouldIgnoreOriginalDechunkedData) {
  // HTTP header is dummy.
  static const char* kResponse =
      "Dummy\r\n\r\n3;n=v\r\ncon\r\n8\r\nsequence\r\n0\r\n\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  chunks.push_back("con");
  size_t remaining = string::npos;

  EXPECT_TRUE(ParseChunkedBody(kResponse,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(0U, remaining);
  EXPECT_EQ(2U, chunks.size());
  const string dechunked = CombineChunks(chunks);
  EXPECT_EQ(11U, dechunked.size());
  EXPECT_EQ("consequence", dechunked);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnFalseWithShortChunk) {
  // HTTP header is dummy.
  static const char* kResponse = "Dummy\r\n\r\n3\r\ncon\r\n8\r\nseq";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining = string::npos;

  EXPECT_FALSE(ParseChunkedBody(kResponse,
                                body_offset, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnFalseIfLengthNotReady) {
  // HTTP header is dummy.
  static const char* kResponse = "Dummy\r\n\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining = string::npos;

  EXPECT_FALSE(ParseChunkedBody(kResponse,
                               body_offset, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnTrueWithIllInput) {
  // HTTP header is dummy.
  static const char* kResponse = "Dummy\r\n\r\n\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;

  EXPECT_TRUE(ParseChunkedBody(kResponse,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnFalseEvenIfSizeIsMuchLarger) {
  // HTTP header is dummy.
  string response = "Dummy\r\n\r\n3\r\na";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_FALSE(ParseChunkedBody(resp,
                                body_offset, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnFalseIfEndWithChunkLength) {
  // HTTP header is dummy.
  string response = "Dummy\r\n\r\n3";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_FALSE(ParseChunkedBody(resp,
                                body_offset, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnTrueIfChunkIsBroken) {
  // HTTP header is dummy.
  string response = "Dummy\r\n\r\n3\r\ncon128\r\nseq";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  absl::string_view resp(response.data(), orig_len);
  EXPECT_TRUE(ParseChunkedBody(resp,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnTrueIfChunkLengthIsBroken) {
  // HTTP header is dummy.
  string response = "Dummy\r\n\r\n3omg_broken_extension\r\nfoo\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  absl::string_view resp(response.data(), orig_len);
  EXPECT_TRUE(ParseChunkedBody(resp,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnFalseIfLengthNotComplete) {
  // HTTP header is dummy.
  string response = "Dummy\r\n\r\n3\r\nfoo\r\n0";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_FALSE(ParseChunkedBody(resp,
                                body_offset, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnTrueIfOffsetIsWrong) {
  // HTTP header is dummy.
  string response = "foo";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_TRUE(ParseChunkedBody(resp,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnTrueIfLengthIsNegativeNumber) {
  // HTTP header is dummy.
  string response = "Dummy\r\n\r\n-1\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_TRUE(ParseChunkedBody(resp,
                               body_offset, &remaining, &chunks));
  EXPECT_EQ(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldReturnFalseIfNoBody) {
  // HTTP header is dummy.
  string response = "dummy\r\n";
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_FALSE(ParseChunkedBody(resp,
                                orig_len, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ShouldParseCrimeMitigation) {
  // CRIME mitigation does followings for obfscating Record Length:
  // 1. Add a particular number of leading zeros to the size string
  // 2. Sub-chunk the body to even smaller chunks
  //
  // See:
  // - go/crime-mitigation-at-gfe-faq
  // - go/crime-mitigation-at-gfe
  static const char* kResponse =
      "HTTP/1.1 200 OK\r\n"
      "Transfer-Encoding: chunked\r\n"
      "Content-Type: text/plain\r\n"
      "\r\n"
      "000004\r\n"
      "abcd\r\n"
      "0016\r\n"
      "efghijklmnopqrstuvwxyz\r\n"
      "0\r\n"
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

  std::vector<absl::string_view> chunks;
  size_t remaining = string::npos;

  EXPECT_TRUE(ParseChunkedBody(kResponse,
                               offset, &remaining, &chunks));
  EXPECT_EQ(0U, remaining);
  EXPECT_EQ(2U, chunks.size());
  const string dechunked = CombineChunks(chunks);
  EXPECT_EQ(26U, dechunked.size());
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz", dechunked);
}

TEST(IoutilTest, ParseChunkedBodyShouldRequireCrlfAfterLastChunk) {
  // HTTP header is dummy.
  string response = "dummy\r\n\r\n0\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_FALSE(ParseChunkedBody(resp,
                                body_offset, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyShouldRequireCrlfAfterTrailer) {
  // HTTP header is dummy.
  string response = "dummy\r\n\r\n0\r\nX-header: x\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_FALSE(ParseChunkedBody(resp,
                                body_offset, &remaining, &chunks));
  EXPECT_GT(remaining, 0U);
  EXPECT_NE(string::npos, remaining);
}

TEST(IoutilTest, ParseChunkedBodyTrailerNotHavingCRLF) {
  // HTTP header is dummy.
  string response = "dummy\r\n\r\n0\r\nX-header: x";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;

  EXPECT_FALSE(ParseChunkedBody(response, body_offset, &remaining, &chunks));
  EXPECT_EQ(remaining, 4U);
}

TEST(IoutilTest, ParseChunkedBodyTrailerEndsWithCR) {
  // HTTP header is dummy.
  string response = "dummy\r\n\r\n0\r\nX-header: x\r";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;

  EXPECT_FALSE(ParseChunkedBody(response, body_offset, &remaining, &chunks));
  EXPECT_EQ(remaining, 3U);
}

TEST(IoutilTest, ParseChunkedBodyTrailerEndsWithCRLF) {
  // HTTP header is dummy.
  string response = "dummy\r\n\r\n0\r\nX-header: x\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;

  EXPECT_FALSE(ParseChunkedBody(response, body_offset, &remaining, &chunks));
  EXPECT_EQ(remaining, 2U);
}

TEST(IoutilTest, ParseChunkedBodyTrailerEndsWithCRLFCR) {
  // HTTP header is dummy.
  string response = "dummy\r\n\r\n0\r\nX-header: x\r\n\r";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;

  EXPECT_FALSE(ParseChunkedBody(response, body_offset, &remaining, &chunks));
  EXPECT_EQ(remaining, 1U);
}

TEST(IoutilTest, ParseChunkedBodyShouldIgnoreTrailer) {
  // HTTP header is dummy.
  string response = "dummy\r\n\r\n0\r\nX-header: x\r\n\r\n";
  const size_t body_offset = 9;  // Index to start HTTP body.
  std::vector<absl::string_view> chunks;
  size_t remaining;
  size_t orig_len = response.size();

  response.resize(1000);
  absl::string_view resp(response.data(), orig_len);
  EXPECT_TRUE(ParseChunkedBody(resp,
                               body_offset, &remaining, &chunks));
}

TEST(IoutilTest, StringRstrip) {
  EXPECT_EQ("abc", StringRstrip("abc"));
  EXPECT_EQ("", StringRstrip(""));
  EXPECT_EQ("abc", StringRstrip("abc\n"));
  EXPECT_EQ("abc", StringRstrip("abc\r\n"));
  EXPECT_EQ("abc", StringRstrip("abc\r"));
  EXPECT_EQ("abc", StringRstrip("abc \r\n"));
  EXPECT_EQ("abc", StringRstrip("abc \r\n\v\f"));
  EXPECT_EQ("ab c", StringRstrip("ab c\r\n"));
  EXPECT_EQ("ab\nc", StringRstrip("ab\nc\r\n"));
  EXPECT_EQ(" abc", StringRstrip(" abc\r\n"));
  EXPECT_EQ("", StringStrip("\r\n "));
}

TEST(IoutilTest, StringStrip) {
  EXPECT_EQ("abc", StringStrip("abc"));
  EXPECT_EQ("", StringStrip(""));
  EXPECT_EQ("abc", StringStrip("\nabc\n"));
  EXPECT_EQ("abc", StringStrip("\r\nabc\r\n"));
  EXPECT_EQ("abc", StringStrip("\rabc\r"));
  EXPECT_EQ("abc", StringStrip(" \r\n abc \r\n"));
  EXPECT_EQ("abc", StringStrip("\v\f \r\n abc \r\n\v\f"));
  EXPECT_EQ("ab c", StringStrip("\r\n ab c\r\n"));
  EXPECT_EQ("ab\nc", StringStrip("\r\n ab\nc\r\n"));
  EXPECT_EQ("", StringStrip("\r\n "));
}

TEST(IoutilTest, ChunkedTransferEncodingWithTwoSpace) {
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

TEST(IoutilTest, ParseQuery) {
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

}  // namespace devtools_goma
