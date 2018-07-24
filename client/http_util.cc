// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "http_util.h"

#include <stdlib.h>

#include "absl/base/attributes.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/strip.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "glog/logging.h"
#include "ioutil.h"

using std::string;

// Handling chunked content transfer encoding.
//
//  RFC 2616 3.6.1 Chunked Transfer Coding
//
//   Chunked-Body = *chunk
//                  last-chunk
//                  trailer
//                  CRLF
//   chunk        = chunk-size [chunk-extension] CRLF
//                  chunk-data CRLF
//   chunk-size   = 1*HEX
//   last-chunk   = 1*("0") [ chunk-extension ] CRLF
//
//   chunk-extension = *( ";" chunk-ext-name [ "=" chunk-ext-val ])
//   chunk-ext-name = token
//   chunk-ext-value = token | quoted-string
//   chunk-data      = chunk-size(OCTET)
//   trailer         = *(entity-header CRLF)
//

namespace {

// Stream scans *non_chunk_data + *input.
class Stream {
 public:
  enum class Status {
    ParseError = -1,
    ParseOk = 0,
    ParseIncomplete = 1,
  };

  Stream(std::string* non_chunk_data, absl::string_view* input,
         std::string* error_message)
      : non_chunk_data_(non_chunk_data),
        input_(input),
        error_message_(error_message) {
  }
  ~Stream() = default;

  Status ConsumePrefix(absl::string_view prefix) {
    absl::string_view buf = Ensure(prefix.size());
    if (buf.empty()) {
      VLOG(2) << "not enough data to match:"
              << absl::CEscape(prefix);
      return Status::ParseIncomplete;
    }
    if (!absl::StartsWith(buf, prefix)) {
      *error_message_ = absl::StrCat(
          "chunk stream got=",
          absl::CEscape(buf.substr(0, prefix.size())),
          " want=",
          absl::CEscape(prefix));
      return Status::ParseError;
    }
    offset_ += prefix.size();
    return Status::ParseOk;
  }

  Status ConsumeUntil(absl::string_view needle) {
    int n = 0;
    do {
      absl::string_view buf = Ensure(n + needle.size());
      if (buf.empty()) {
        VLOG(2) << "not enough data to finding " << absl::CEscape(needle);
        return Status::ParseIncomplete;
      }
      if (absl::EndsWith(buf, needle)) {
        offset_ = non_chunk_data_->size();
        return Status::ParseOk;
      }
      ++n;
    } while (!input_->empty());
    return Status::ParseIncomplete;
  }

  Status ConsumeSize(size_t* size) {
    bool size_found = false;
    *size = 0;
    do {
      absl::string_view buf = Ensure(1);
      if (buf.empty()) {
        VLOG(2) << "not enough data for size:"
                << absl::CEscape(*non_chunk_data_)
                << " offset=" << offset_;
        return Status::ParseIncomplete;
      }
      char ch = buf[0];
      if (!absl::ascii_isxdigit(ch)) {
        if (!size_found) {
          *error_message_ = absl::StrCat("no size found at=",
                                         absl::CEscape(buf.substr(0, 1)));
          return Status::ParseError;
        }
        if (ch == '\r' || ch == ';') {
          VLOG(2) << "chunk-size=" << *size;
          return Status::ParseOk;
        }
        *error_message_ = absl::StrCat("chunk-size wrong data=",
                                       absl::CEscape(buf.substr(0, 1)));
        return Status::ParseError;
      }
      if ((std::numeric_limits<size_t>::max() >> 4) < *size) {
        *error_message_ = "chunk-size overflow";
        return Status::ParseError;
      }
      *size <<= 4;
      if (ch >= 'a' && ch <= 'f') {
        *size += ch - 'a' + 10;
      } else if (ch >= 'A' && ch <= 'F') {
        *size += ch - 'A' + 10;
      } else {
        CHECK(absl::ascii_isdigit(ch)) << ch;
        *size += ch - '0';
      }
      size_found = true;
      offset_++;
    } while (!input_->empty());
    return Status::ParseIncomplete;
  }

 private:
  // Ensure size available in *non_chunk_data_.
  // adds data from *input_ if needed.
  // Returns buf that at least size bytes available.
  // Returns empty string view if size bytes is not available.
  absl::string_view Ensure(size_t size) {
    absl::string_view buf(*non_chunk_data_);
    buf.remove_prefix(offset_);
    VLOG(3) << "need=" << size << " buf:" << CEscape(buf);
    if (size <= buf.size()) {
      VLOG(3) << "buf:" << CEscape(buf);
      return buf;
    }
    size_t need = offset_ + size - non_chunk_data_->size();
    VLOG(3) << "need=" << need << " input size:" << input_->size();
    if (need <= input_->size()) {
      *non_chunk_data_ += std::string(input_->substr(0, need));
      input_->remove_prefix(need);
      buf = *non_chunk_data_;
      buf.remove_prefix(offset_);
      CHECK_LE(size, buf.size());
      VLOG(3) << "buf:" << CEscape(buf);
      return buf;
    }
    *non_chunk_data_ += std::string(*input_);
    input_->remove_prefix(input_->size());
    return absl::string_view();
  }

  std::string* non_chunk_data_;
  absl::string_view* input_;
  std::string* error_message_;
  size_t offset_ = 0UL;
};

}  // anonymous namespace

namespace devtools_goma {

ABSL_CONST_INIT const absl::string_view kAcceptEncoding = "Accept-Encoding";
ABSL_CONST_INIT const absl::string_view kAuthorization = "Authorization";
ABSL_CONST_INIT const absl::string_view kContentEncoding = "Content-Encoding";
ABSL_CONST_INIT const absl::string_view kContentLength = "Content-Length";
ABSL_CONST_INIT const absl::string_view kContentType = "Content-Type";
ABSL_CONST_INIT const absl::string_view kCookie = "Cookie";
ABSL_CONST_INIT const absl::string_view kHost = "Host";
ABSL_CONST_INIT const absl::string_view kUserAgent = "User-Agent";
ABSL_CONST_INIT const absl::string_view kTransferEncoding = "Transfer-Encoding";

// Parse HTTP request and response headers and return offset into body
// and content-length. Content-Length may be missing, and in that case
// content_length will be set to string::npos.
// TODO: to be more conformant to the http standard
bool FindContentLengthAndBodyOffset(
    absl::string_view data, size_t *content_length, size_t *body_offset,
    bool *is_chunked) {
  const char kChunked[] = "chunked";
  const char kCrlf[] = "\r\n";
  const absl::string_view::size_type content_length_pos =
      data.find(absl::StrCat(kContentLength, ": "));
  const absl::string_view::size_type transfer_encoding_pos =
      data.find(absl::StrCat(kTransferEncoding, ": "));
  const absl::string_view::size_type response_body = data.find("\r\n\r\n");

  if (response_body == absl::string_view::npos) {
    LOG(ERROR) << "GOMA: Invalid, missing CRLFCRLF";
    return false;
  }
  *body_offset = response_body + 4;

  if (content_length_pos == absl::string_view::npos) {
    // Content-Length does not exist for GET requests. This might be
    // such request. If so, assume the header is short and return here.
    *content_length = string::npos;
  } else  if (content_length_pos >= response_body) {
    // The content_length string is not in the header, but in the
    // payload. That means we don't have Content-Length, and we don't
    // know how much further we should read.
    *content_length = string::npos;
  } else {
    absl::string_view lenstr =
        data.substr(content_length_pos +
                    kContentLength.size() +  2);
    *content_length = atoi(string(lenstr).c_str());
  }

  if (is_chunked != nullptr) {
    if (transfer_encoding_pos == absl::string_view::npos) {
      // Transfer-Encoding does not exist for GET requests.
      *is_chunked = false;
    } else if (transfer_encoding_pos >= response_body) {
      // The Transfer-Encoding string is not in the header.
      *is_chunked = false;
    } else {
      // The Transfer-Encoding string is in the header.
      // We should check its value is "chunked" or not.
      absl::string_view transfer_encoding_value = data.substr(
          transfer_encoding_pos +
          kTransferEncoding.size() + 2);
      absl::string_view::size_type value_end =
          transfer_encoding_value.find(kCrlf);
      transfer_encoding_value = StringStrip(
          transfer_encoding_value.substr(0, value_end));
      if (transfer_encoding_value == kChunked) {
        *is_chunked = true;
      } else {
        *is_chunked = false;
      }
    }
  }

  return true;
}

// Parse the HTTP response header.
// Return true if it got whole header, or error response.
// Return false if it needs more data.
bool ParseHttpResponse(absl::string_view response,
                       int* http_status_code,
                       size_t* offset,
                       size_t* content_length,
                       bool* is_chunked) {
  *http_status_code = 0;
  *offset = 0;
  *content_length = string::npos;
  if (is_chunked != nullptr)
    *is_chunked = false;

  // Check the return code from server. It should be "HTTP/1.? 200 OK\r\n"
  const char kHttpHeader[] = "HTTP/1.";
  // + 2 for the minor version and + 4 for status code.
  if (response.size() < strlen(kHttpHeader) + 2 + 4)
    return false;

  if (strncmp(response.data(), kHttpHeader, strlen(kHttpHeader)) != 0) {
    LOG(ERROR) << kHttpHeader << " expected, but got "
               << string(response.data(), strlen(kHttpHeader));
    return true;
  }

  absl::string_view codestr = response.substr(strlen(kHttpHeader) + 2);
  *http_status_code = atoi(string(codestr).c_str());
  if (*http_status_code != 200 && *http_status_code != 204)
    return true;

  if (!FindContentLengthAndBodyOffset(response, content_length, offset,
                                      is_chunked)) {
    return false;
  }

  VLOG(3) << "HTTP header=" << response.substr(0, *offset);
  return true;
}

std::map<string, string> ParseQuery(const string& query) {
  std::map<string, string> params;
  if (query.empty()) {
    return params;
  }
  string query_str = query;
  size_t pos = query_str.find('#');
  if (pos != string::npos) {
    query_str = query.substr(0, pos);
  }

  for (auto&& p : absl::StrSplit(query_str, '&', absl::SkipEmpty())) {
    size_t i = p.find('=');
    if (i == string::npos) {
      params.insert(make_pair(string(p), ""));
      continue;
    }
    string k(p.substr(0, i));
    string v(p.substr(i + 1));
    // TODO: url decode?
    params.insert(make_pair(k, v));
  }
  return params;
}

string SimpleEncodeChartData(const std::vector<double>& value, double max) {
  std::ostringstream ss;
  for (const auto& iter : value) {
    int v = static_cast<int>(62 * iter / max);
    if (v < 0) {
      ss << "_";
    } else if (v < 26) {
      ss << static_cast<char>('A' + v);
    } else if (v < 52) {
      ss << static_cast<char>('a' + v - 26);
    } else if (v < 62) {
      ss << static_cast<char>('0' + v - 52);
    } else {
      ss << "9";
    }
  }
  return ss.str();
}

HttpChunkParser::HttpChunkParser()
    : non_chunk_data_("\r\n") {
}

bool HttpChunkParser::Parse(absl::string_view input,
                            std::vector<absl::string_view>* pieces) {
  done_ = false;
  VLOG(2) << "parse chunk stream";
  while (!input.empty()) {
    if (last_chunk_remain_ > 0) {
      VLOG(1) << "last_chunk_remain=" << last_chunk_remain_;
      CHECK(non_chunk_data_.empty()) << absl::CEscape(non_chunk_data_);
      if (last_chunk_remain_ >= input.size()) {
        pieces->push_back(input);
        last_chunk_remain_ -= input.size();
        VLOG(1) << "chunk-data incomplete. still need " << last_chunk_remain_;
        return true;
      }
      pieces->push_back(input.substr(0, last_chunk_remain_));
      input.remove_prefix(last_chunk_remain_);
      VLOG(1) << "chunk-data done";
    }
    last_chunk_remain_ = 0;
    Stream stream(&non_chunk_data_, &input, &error_message_);
    Stream::Status s = stream.ConsumePrefix("\r\n");
    switch (s) {
      case Stream::Status::ParseError:
        return false;
      case Stream::Status::ParseIncomplete:
        VLOG(1) << "need more data for CRLF at the end of chunk-data"
                << absl::CEscape(non_chunk_data_);
        return true;
      case Stream::Status::ParseOk:
        break;
    }
    size_t size = 0;
    s = stream.ConsumeSize(&size);
    switch (s) {
      case Stream::Status::ParseError:
        return false;
      case Stream::Status::ParseIncomplete:
        VLOG(1) << "need more data for chunk-size:"
                << absl::CEscape(non_chunk_data_);
        return true;
      case Stream::Status::ParseOk:
        break;
    }
    if (size == 0) {
      // last chunk. skip trailer.
      VLOG(1) << "skip trailer";
      s = stream.ConsumeUntil("\r\n\r\n");
      switch (s) {
        case Stream::Status::ParseError:
          LOG(FATAL) << "parse error to find CRLFCRLF?";
        case Stream::Status::ParseIncomplete:
          VLOG(1) << "need more data for trailer:"
                  << absl::CEscape(non_chunk_data_);
          return true;
        case Stream::Status::ParseOk:
          break;
      }
      VLOG(1) << "all chunked-body received";
      done_ = true;
      return true;
    }
    // skip chunk-extension.
    VLOG(1) << "skip chunk-extension";
    s = stream.ConsumeUntil("\r\n");
    switch (s) {
      case Stream::Status::ParseError:
        LOG(FATAL) << "parse error to find CRLF?";
      case Stream::Status::ParseIncomplete:
        VLOG(1) << "need more data for chunk-extension:"
                << absl::CEscape(non_chunk_data_);
        return true;
      case Stream::Status::ParseOk:
        break;
    }
    non_chunk_data_.clear();
    last_chunk_remain_ = size;
    VLOG(1) << "next chunk-size=" << last_chunk_remain_;
  }
  VLOG(1) << "no more data in buffer. need more data"
          << " last_chunk_remain=" << last_chunk_remain_
          << " non_chunk_data=" << non_chunk_data_;
  return true;
}

}  // namespace devtools_goma
