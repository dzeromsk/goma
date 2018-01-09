// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_FLAGS_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_FLAGS_UTIL_H_

#ifndef _WIN32

#include <string>
#include <vector>

using std::string;

namespace devtools_goma {

class CompilerInfo;

class CompilerFlagsUtil {
 public:
  // Changes path names used in args to be relative from cwd as much as
  // possible.  If a path name is under system include paths specified
  // by compiler_info, or totally different path from cwd, it remains
  // as absolute path.
  // For example, when cwd = /home/goma/src/WebKit/WebKitBuild
  //     -I/home/goma/src/WebKit/Source/WebKit
  //  => -I../Source/WebKit
  //     -o /home/goma/src/WebKit/WebKitBuild/foo.o
  //  => -o foo.o
  //
  //     -I/usr/include
  //  => -I/usr/include  # it is system path.
  //
  //     -o /tmp/bar.o
  //  => -o /tmp/bar.o   # /home != /tmp
  static std::vector<string> MakeWeakRelative(
      const std::vector<string>& args,
      const string& cwd,
      const CompilerInfo& compiler_info);
};

}  // namespace devtools_goma

#endif  // _WIN32
#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_FLAGS_UTIL_H_
