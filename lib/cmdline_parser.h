// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_CMDLINE_PARSER_H_
#define DEVTOOLS_GOMA_LIB_CMDLINE_PARSER_H_

#include <string>
#include <vector>

using std::string;

namespace devtools_goma {

// Parsing Command-Line Arguments (on posix for gcc, javac)
// Note: parsed |cmdline| will be appended to argv.
bool ParsePosixCommandLineToArgv(const string& cmdline,
                                 std::vector<string>* argv);

// Parsing Command-Line Arguments (on Windows)
// Note: parsed |cmdline| will be appended to argv.
bool ParseWinCommandLineToArgv(const string& cmdline,
                               std::vector<string>* argv);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_CMDLINE_PARSER_H_
