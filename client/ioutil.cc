// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "ioutil.h"

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

#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "basictypes.h"
#include "glog/logging.h"
#include "scoped_fd.h"
#include "zlib.h"

using std::string;

namespace devtools_goma {

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

class ScopedFdWriteCloser : public WriteCloser {
 public:
  explicit ScopedFdWriteCloser(ScopedFd&& fd) : fd_(std::move(fd)) {}
  ~ScopedFdWriteCloser() override = default;

  ssize_t Write(const void* ptr, size_t len) override {
    return fd_.Write(ptr, len);
  }
  bool Close() override { return fd_.Close(); }

 private:
  ScopedFd fd_;
};

class GzipInflateWriteCloser : public WriteCloser {
 public:
  explicit GzipInflateWriteCloser(std::unique_ptr<WriteCloser> wr)
      : wr_(std::move(wr)) {
    zcontext_.state = Z_NULL;
    zcontext_.zalloc = Z_NULL;
    zcontext_.zfree = Z_NULL;
    zcontext_.opaque = Z_NULL;
    zcontext_.next_in = nullptr;
    zcontext_.avail_in = 0;
    zcontext_.total_in = 0;
    output_buffer_ = absl::make_unique<char[]>(kDefaultBufSize);
    zcontext_.next_out = reinterpret_cast<Bytef*>(&output_buffer_[0]);
    zcontext_.avail_out = kDefaultBufSize;
    zcontext_.total_out = 0;
    const int kWindowBits = 15;
    const int kWindowBitsFormat = 16;  // GZIP
    zerror_ = inflateInit2(&zcontext_, kWindowBits | kWindowBitsFormat);
  }
  ~GzipInflateWriteCloser() override { inflateEnd(&zcontext_); }

  ssize_t Write(const void* ptr, size_t len) override {
    if (zerror_ != Z_OK) {
      return -1;
    }
    DCHECK_GT(len, 0);
    zcontext_.next_in = static_cast<const Bytef*>(ptr);
    zcontext_.avail_in = len;
    do {
      zcontext_.next_out = reinterpret_cast<Bytef*>(&output_buffer_[0]);
      zcontext_.avail_out = kDefaultBufSize;
      zerror_ = inflate(&zcontext_, Z_NO_FLUSH);
      if (zerror_ != Z_OK && zerror_ != Z_STREAM_END) {
        return len - zcontext_.avail_in;
      }
      size_t wlen =
          zcontext_.next_out - reinterpret_cast<Bytef*>(&output_buffer_[0]);
      ssize_t written = wr_->Write(&output_buffer_[0], wlen);
      if (written != wlen) {
        return len - zcontext_.avail_in;
      }
    } while (zcontext_.avail_in > 0);
    return len;
  }

  bool Close() override {
    bool r = wr_->Close();
    if (zerror_ != Z_STREAM_END) {
      return false;
    }
    return r;
  }

 private:
  static const int kDefaultBufSize = 65536;
  std::unique_ptr<WriteCloser> wr_;
  z_stream zcontext_;
  int zerror_;
  std::unique_ptr<char[]> output_buffer_;
};

/* static */
std::unique_ptr<WriteCloser> WriteCloser::NewFromScopedFd(ScopedFd&& fd) {
  return absl::make_unique<ScopedFdWriteCloser>(std::move(fd));
}

/* static */
std::unique_ptr<WriteCloser> WriteCloser::NewGzipInflate(
    std::unique_ptr<WriteCloser> wr) {
  return absl::make_unique<GzipInflateWriteCloser>(std::move(wr));
}

void FlushLogFiles() {
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
  google::FlushLogFiles(google::INFO);
#else
  google::FlushLogFiles(google::GLOG_INFO);
#endif
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

}  // namespace devtools_goma
