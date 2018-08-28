// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_IOUTIL_H_
#define DEVTOOLS_GOMA_CLIENT_IOUTIL_H_

#ifdef _WIN32
#include "config_win.h"
#endif

#include <memory>
#include <string>

#include "absl/strings/string_view.h"

using std::string;

namespace devtools_goma {

class ScopedFd;

// Win32 std::cout, std::cerr open as text mode, so cout << "foo\r\n" emits
// "foo\r\r\n".  It is not ninja friendly.
// b/6617503
void WriteStdout(absl::string_view data);
void WriteStderr(absl::string_view data);

// WriteCloser is an interface to write streamed data and close.
class WriteCloser {
 public:
  static std::unique_ptr<WriteCloser> NewFromScopedFd(ScopedFd&& fd);
  static std::unique_ptr<WriteCloser> NewGzipInflate(
      std::unique_ptr<WriteCloser> wr);

  virtual ~WriteCloser() = default;

  // Write writes data in ptr[0:len), and returns number of data written in ptr.
  // i.e. returns len if all data were successfully written.
  // negative means that it failed to write.
  // less than len means partial write.
  // len must be > 0.
  virtual ssize_t Write(const void* ptr, size_t len) = 0;

  // Close closes the writer. returns true when success. false on error.
  virtual bool Close() = 0;
};

void FlushLogFiles();

// Escape strings as javascript string.
// TODO: move to json_util?
string EscapeString(const string& str);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_IOUTIL_H_
