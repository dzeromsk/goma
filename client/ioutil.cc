// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "ioutil.h"

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#else
# include "config_win.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "basictypes.h"
#include "file.h"
#include "file_dir.h"
#include "file_stat.h"
#include "filesystem.h"
#include "glog/logging.h"
#include "path_util.h"
#include "scoped_fd.h"

using std::string;

namespace {

// Come from Python 2.7 string.whitespace.
static const char* kWhitespaces = "\t\n\x0b\x0c\r ";

}  // namespace

namespace devtools_goma {

// Parse HTTP request and response headers and return offset into body
// and content-length. Content-Length may be missing, and in that case
// content_length will be set to string::npos.
// TODO: to be more conformant to the http standard
bool FindContentLengthAndBodyOffset(
    absl::string_view data, size_t *content_length, size_t *body_offset,
    bool *is_chunked) {
  const char kContentLength[] = "Content-Length: ";
  const char kTransferEncoding[] = "Transfer-Encoding: ";
  const char kChunked[] = "chunked";
  const char kCrlf[] = "\r\n";
  const absl::string_view::size_type content_length_pos =
      data.find(kContentLength);
  const absl::string_view::size_type transfer_encoding_pos =
      data.find(kTransferEncoding);
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
        data.substr(content_length_pos + strlen(kContentLength));
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
          transfer_encoding_pos + strlen(kTransferEncoding));
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

absl::string_view StringRstrip(absl::string_view str) {
  size_t found = str.find_last_not_of(kWhitespaces);
  if (found != string::npos)
    return str.substr(0, found + 1);
  return str.substr(str.size(), 0);  // empty string piece.
}

absl::string_view StringStrip(absl::string_view str) {
  absl::string_view::size_type found = str.find_last_not_of(kWhitespaces);
  if (found == absl::string_view::npos)
    return str.substr(str.size(), 0);  // empty string piece.
  str = str.substr(0, found + 1);
  found = str.find_first_not_of(kWhitespaces);
  return str.substr(found);
}

void WriteStringToFileOrDie(const string &data, const string &filename,
                            int permission) {
  ScopedFd fd(ScopedFd::Create(filename, permission));
  if (!fd.valid()) {
    PLOG(FATAL) << "GOMA: failed to open " << filename;
  }
  if (fd.Write(data.c_str(), data.size()) !=
      static_cast<ssize_t>(data.size())) {
    PLOG(FATAL) << "GOMA: Cannot write to file " << filename;
  }
}

void AppendStringToFileOrDie(const string &data, const string &filename,
                             int permission) {
  ScopedFd fd(ScopedFd::OpenForAppend(filename, permission));
  if (!fd.valid()) {
    PLOG(FATAL) << "GOMA: failed to open " << filename;
  }
  if (fd.Write(data.c_str(), data.size()) !=
      static_cast<ssize_t>(data.size())) {
    PLOG(FATAL) << "GOMA: Cannot write to file " << filename;
  }
}

void WriteStdout(absl::string_view data) {
#ifdef _WIN32
  HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD bytes_written = 0;
  if (!WriteFile(stdout_handle,
                 data.data(), data.size(),
                 &bytes_written, nullptr)) {
    LOG_SYSRESULT(GetLastError());
  }
#else
  std::cout << data << std::flush;
#endif
}

void WriteStderr(absl::string_view data) {
#ifdef _WIN32
  HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
  DWORD bytes_written = 0;
  if (!WriteFile(stderr_handle,
      data.data(), data.size(),
      &bytes_written, nullptr)) {
    LOG_SYSRESULT(GetLastError());
  }
#else
  std::cerr << data;
#endif
}

void FlushLogFiles() {
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
  google::FlushLogFiles(google::INFO);
#else
  google::FlushLogFiles(google::GLOG_INFO);
#endif
}

void GetBaseDir(const string& filepath, string* base_dir) {
#ifndef _WIN32
  const char SEP = '/';
#else
  const char SEP = '\\';
#endif
  size_t i = filepath.rfind(SEP);
  if (i == string::npos) {
    *base_dir = ".";
  } else {
    while (i > 0 && filepath[i - 1] == SEP) {
      i--;
    }
    *base_dir = filepath.substr(0, i + 1);
  }
}

string GetCurrentDirNameOrDie(void) {
#ifndef _WIN32
  // get_cwd() returns the current resolved directory. However, a compiler is
  // taking PWD as current working directory. PWD might contain unresolved
  // directory.
  // We don't return /proc/self/cwd if it is set in PWD, since the corresponding
  // directory is different among gomacc and compiler_proxy.
  // See also: b/37259278

  const char* pwd = getenv("PWD");
  if (pwd != nullptr && IsPosixAbsolutePath(pwd) &&
      !HasPrefixDir(pwd, "/proc/self/cwd")) {
    // Align with llvm current_path().
    // llvm checking PWD id and "." id are the same.
    FileStat pwd_stat(pwd);
    FileStat dot_stat(".");
    if (pwd_stat.IsValid() && dot_stat.IsValid() && pwd_stat.is_directory &&
        pwd_stat == dot_stat) {
      return pwd;
    }
  }

  char *dir = getcwd(nullptr, 0);
  CHECK(dir) << "GOMA: Cannot find current directory ";
  string dir_str(dir);
  free(dir);
  return dir_str;
#else
  char dir[PATH_MAX];
  CHECK_NE(GetCurrentDirectoryA(PATH_MAX, dir), (DWORD)0) <<
      "GOMA: Cannot find current directory: " << GetLastError();
  string dir_str(dir);
  return dir_str;
#endif
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
  if (is_chunked != nullptr && *is_chunked) {
    return true;
  }

  if (*content_length == string::npos) {
    return true;
  }

  if (response.size() < *offset + *content_length) {
    // if response size is too small, there was some network error.
    return false;
  }
  return true;
}

void DeleteRecursivelyOrDie(const string& dirname) {
  CHECK(file::RecursivelyDelete(dirname, file::Defaults()).ok()) << dirname;
}

string EscapeString(const string& str) {
  std::stringstream escaped_str;
  escaped_str << "\"";
  for (size_t i = 0; i < str.size(); ++i) {
    switch (str[i]) {
      case '"': escaped_str << "\\\""; break;
      case '\\': escaped_str << "\\\\"; break;
      case '\b': escaped_str << "\\b"; break;
      case '\f': escaped_str << "\\f"; break;
      case '\n': escaped_str << "\\n"; break;
      case '\r': escaped_str << "\\r"; break;
      case '\t': escaped_str << "\\t"; break;
      case '\033':
        {
          // handle escape sequence.
          // ESC[1m  -> bold
          // ESC[0m  -> reset
          // ESC[0;<bold><fgbg><color>m -> foreground
          //  <bold> "1;" or ""
          //  <fgbg> "3" foreground or "4" background
          //  <color> 0 black / 1 red / 2 green / 4 blue
          // For now, just ignore these escape sequence.
          size_t next_i = i;
          size_t j = i;
          if (j + 2 < str.size() && str[j + 1] == '[') {
            for (j += 2; j < str.size(); ++j) {
              if (str[j] == ';' || (isdigit(str[j])))
                continue;
              if (str[j] == 'm')
                next_i = j;
              break;
            }
          }
          if (next_i != i) {
            i = next_i;
            break;
          }
        }
        FALLTHROUGH_INTENDED;
      default:
        if (str[i] < 0x20) {
          escaped_str << "\\u" << std::hex << std::setw(4)
                      << std::setfill('0') << static_cast<int>(str[i]);
        } else {
          escaped_str << str[i];
        }
    }
  }
  escaped_str << "\"";
  return escaped_str.str();
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

// Parse chunked transfer coding.
// You SHOULD NOT indicates trailers in a TE header of a request since we do
// not expect important headers in the trailers.  In other words, we just
// discard trailers.
//
// Reference: RFC2616 3.6.1 Chunked Transfer Coding.
bool ParseChunkedBody(absl::string_view response,
                      size_t offset,
                      size_t* remaining_chunk_length,
                      std::vector<absl::string_view>* chunks) {
  size_t head = offset;
  *remaining_chunk_length = string::npos;
  chunks->clear();

  if (head > response.size()) {
    LOG(ERROR) << "Given offset is shorter than response length."
               << " response_len=" << response.size()
               << " offset=" << offset;
    return true;
  }

  while (head < response.size()) {
    if (!isxdigit(response[head])) {
      LOG(ERROR) << "Expected hexdigit but got:" << (int)response[head];
      LOG(ERROR) << " response_len=" << response.size()
                 << " head=" << head;
      LOG(ERROR) << "broken chunk:" << response;
      return true;
    }
    char *endptr;
    const unsigned long chunk_length =
        strtoul(response.data() + head, &endptr, 16);
    if (endptr >= response.data() + response.size()) {
      // reached the end of response.
      *remaining_chunk_length = chunk_length + 4;
      return false;
    }
    if (*endptr != '\r' && *endptr != ';') {
      LOG(ERROR) << "Unexpected character after length:"
                 << *endptr;
      return true;
    }

    if (chunk_length == 0) {  // last chunk.
      VLOG(2) << "Found last-chunk.";
      // Confirm the remaining of resp should be like:
      // 0; chunk-extension CRLF
      // trailer
      // CRLF

      // skip chunk-extension.
      absl::string_view::size_type crlf_pos = response.find("\r\n", head);
      if (crlf_pos == absl::string_view::npos) {
        // need more data.
        // 4 comes from \r\n<trailer (which can be omitted)>\r\n.
        *remaining_chunk_length = 4;
        return false;
      }

      head = crlf_pos + 2;

      // skip trailer.
      while (head < response.size()) {
        // incomplete CR after trailer headers
        if (response.substr(head) == "\r") {
          *remaining_chunk_length = 1;
          return false;
        }

        // CRLF after trailer headers
        if (response.substr(head) == "\r\n") {
          *remaining_chunk_length = 0;
          return true;
        }

        crlf_pos = response.find("\r\n", head);

        if (crlf_pos == absl::string_view::npos) {
          // incomplete trailer header ends with CR
          if (absl::EndsWith(response, "\r")) {
            *remaining_chunk_length = 3;
            return false;
          }

          // incomplete trailer header not include CRLF
          *remaining_chunk_length = 4;
          return false;
        }

        LOG(WARNING) << "Ignoring Chunked Transfer Coding trailer: "
                     << response.substr(head, crlf_pos - head);
        head = crlf_pos + 2;
      }

      // need one more CRLF after trailer headers
      *remaining_chunk_length = 2;
      return false;
    }

    VLOG(2) << "resp len:" << response.size()
            << ", head:" << head
            << ", chunk_len:" << chunk_length;
    // skip chunk-extension.
    absl::string_view::size_type crlf_pos = response.find("\r\n", head);
    if (crlf_pos == absl::string_view::npos) {
      // need more data.
      // 4 comes from \r\n<chunk>\r\n.
      *remaining_chunk_length = chunk_length + 4;
      return false;
    }
    if (response.size() < crlf_pos + chunk_length + 4) {
      // need more data.
      // 4 comes from \r\n<chunk>\r\n.
      *remaining_chunk_length = crlf_pos + chunk_length + 4 - response.size();
      return false;
    }

    head = crlf_pos + 2;
    chunks->push_back(response.substr(head, chunk_length));
    if (strncmp(response.data() + head + chunk_length, "\r\n", 2)) {
      LOG(ERROR) << "chunk does not end with expected CRLF.:"
                 << "Actual: " << response.substr(head, 2);
      return true;
    }
    head += chunk_length + 2;
  }
  // Need more data.  However, I do not know how much remains.
  // All chunks has read but last chunk's size is not 0.
  // This means at least one chunk will come.
  // 0;<chunk-extension>\r\n<trailers>\r\n.
  *remaining_chunk_length = 5;
  return false;
}

string CombineChunks(const std::vector<absl::string_view>& chunks) {
  string dechunked;
  for (const auto& it : chunks) {
    dechunked.append(it.data(), it.size());
  }
  return dechunked;
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

}  // namespace devtools_goma
