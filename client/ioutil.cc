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

#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "basictypes.h"
#include "file_dir.h"
#include "filesystem.h"
#include "glog/logging.h"
#include "scoped_fd.h"

using std::string;

namespace devtools_goma {

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
