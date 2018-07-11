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

// Escape strings as javascript string.
string EscapeString(const string& str);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_IOUTIL_H_
