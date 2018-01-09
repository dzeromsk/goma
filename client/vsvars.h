// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_VSVARS_H_
#define DEVTOOLS_GOMA_CLIENT_VSVARS_H_

#ifndef _WIN32
#error This module is Windows only
#endif

#include <set>
#include <string>

using std::string;

namespace devtools_goma {

// Gets VC InstallDir from |reg_path| in HKEY_LOCAL_MACHINE.
// Returns a path in InstallDir registry, e.g
// c:\Program Files (x86)\Microsoft Visual Studio 12.0\Common7\IDE
// Returns empty string if not found.
string GetVCInstallDir(const string& reg_path);

// Gets vsvars32.bat path for |vs_version|.
// |vs_version| is something like "12.0", "11.0", etc.
// For example:
//   "12.0" -> Visual Studio 2013
//   "11.0" -> Visual Studio 2012
//   "10.0" -> Visual Studio 2010
void GetVSVarsPath(string vs_version, std::set<string>* vsvars);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_VSVARS_H_
