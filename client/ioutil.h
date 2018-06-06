// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_IOUTIL_H_
#define DEVTOOLS_GOMA_CLIENT_IOUTIL_H_

#include <map>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#ifdef _WIN32
#include "socket_helper_win.h"
#endif

using std::string;

namespace devtools_goma {

const int kNetworkBufSize = 1024 * 32;
const int kReadSelectTimeoutSec = 20;

class ScopedSocket;

// Removes tailing spaces from |str|.
absl::string_view StringRstrip(absl::string_view str);

// Removes leading and tailing spaces from |str|.
absl::string_view StringStrip(absl::string_view str);

void WriteStringToFileOrDie(const string &data, const string &filename,
                            int permission);

void AppendStringToFileOrDie(const string &data, const string &filename,
                             int permission);

// Win32 std::cout, std::cerr open as text mode, so cout << "foo\r\n" emits
// "foo\r\r\n".  It is not ninja friendly.
// b/6617503
void WriteStdout(absl::string_view data);
void WriteStderr(absl::string_view data);

void FlushLogFiles();

// Get current directory.
string GetCurrentDirNameOrDie(void);

// Get base directory path of the given |filepath|.
void GetBaseDir(const string& filepath, string* base_dir);

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

void DeleteRecursivelyOrDie(const string& dirname);

string EscapeString(const string& str);

// http://code.google.com/apis/chart/docs/data_formats.html#simple
string SimpleEncodeChartData(const std::vector<double>& value, double max);

// Parse body encoded with chunked transfer coding.
// Return true if whole chunks parsed, or error.
// Return false if it needs more data.
//
// remaining_chunk_length:
// - 0: success (returns true).
// - string::npos: error (returns true).
// - otherwise, need more data (returns false).
//
// chunks is set only when ParseChunkedBody returns true and
// *remaining_chunk_length == 0.
bool ParseChunkedBody(absl::string_view response,
                      size_t offset, size_t* remaining_chunk_length,
                      std::vector<absl::string_view>* chunks);

string CombineChunks(const std::vector<absl::string_view>& chunks);

std::map<string, string> ParseQuery(const string& query);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_IOUTIL_H_
