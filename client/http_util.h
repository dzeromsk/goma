// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_HTTP_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_HTTP_UTIL_H_

// Utilities for HTTP.
// TODO: move http funcs from ioutil.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace devtools_goma {

const int kNetworkBufSize = 1024 * 32;

extern const absl::string_view kAcceptEncoding;
extern const absl::string_view kAuthorization;
extern const absl::string_view kContentEncoding;
extern const absl::string_view kContentLength;
extern const absl::string_view kContentType;
extern const absl::string_view kCookie;
extern const absl::string_view kHost;
extern const absl::string_view kUserAgent;
extern const absl::string_view kTransferEncoding;

// Parse the HTTP response header.
// Return true if it got all header, or error response.
// Return false if it needs more data.
//
// In case of returning true with error, |http_status_code| will not be
// 200 or 204.  You must not use other fields in such a case.
//
// If returning true without error, followings could be set:
// |http_status_code| represents HTTP status code.
// |offset| represents offset where HTTP body starts.
// |content_length| represents value of Content-Length header if exists.
// If no Content-Length header found in the header, |content_length| is set to
// string::npos.
// |is_chunked| become true if HTTP response is sent with chunked transfer
// encoding. Note that the function will not check chunked transfer coding
// if |is_chunked| == NULL.
bool ParseHttpResponse(absl::string_view response,
                       int* http_status_code,
                       size_t* offset,
                       size_t* content_length,
                       bool* is_chunked);

// Parse HTTP request and response headers and return offset into body
// and content-length. Content-Length may be missing, and in that case
// content_length will be set to string::npos.
// If data is encoded with chunked transfer encoding, is_chunked will be
// set to true.
//
// Do not check chunked transfer coding if is_chunked == NULL.
bool FindContentLengthAndBodyOffset(
    absl::string_view data, size_t *content_length, size_t *body_offset,
    bool *is_chunked);

// Parse http request query parameter.
std::map<std::string, std::string> ParseQuery(const std::string& query);

// http://code.google.com/apis/chart/docs/data_formats.html#simple
std::string SimpleEncodeChartData(
    const std::vector<double>& value, double max);

class HttpChunkParser {
 public:
  HttpChunkParser();
  ~HttpChunkParser() = default;

  // Parse parses chunked transfer encoding from stream
  // into *pieces, and returns true.
  // All chunk-data in stream will be appended into *pieces.
  // chunk-data may be several pieces, if boundary is in chunk-data.
  // It returns false if it failed to parse chunked transfer encoding.
  bool Parse(absl::string_view stream,
             std::vector<absl::string_view>* pieces);

  // Returns true if chunked transfer encoding is finished
  // in the last Parse.
  bool done() const {
    return done_;
  }

  // Returns error message after Parse returns false.
  const std::string& error_message() const {
    return error_message_;
  }

 private:
  // last_chunk_remain is a number of remaining bytes of the last chunk.
  size_t last_chunk_remain_ = 0UL;

  // holds non chunk-data parts.
  // "CRLF chunk-size [chunk-extension] CRLF" or
  // "CRLF last-chunk trailer CRLF".
  std::string non_chunk_data_;

  // If it got all chunked-body, i.e. it sees "last-chunk trailer CRLF",
  // it sets done_ to true.  In other words, if done_ is false, need more
  // data.
  bool done_ = false;

  std::string error_message_;
};


}  // namespace devtools_goma

#endif // DEVTOOLS_GOMA_CLIENT_HTTP_UTIL_H_
