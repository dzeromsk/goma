// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_MYPATH_H_
#define DEVTOOLS_GOMA_CLIENT_MYPATH_H_

#include <string>

using std::string;

namespace devtools_goma {

// Gets username who execute this program from environment variable.
// Note: it won't return correct username in gomacc/win, or gomacc
// under scons, etc.
// Returns empty string if not found.
string GetUsernameEnv();

// Get username who execute this program without environment variable.
// Returns empty string if not found.
string GetUsernameNoEnv();

// Get username who execute this program from environment variable,
// or system call.  It will set username in $USER.
// Returns "unknown" if not found.
string GetUsername();

// Gets nodename/hostname which this program runs on.
string GetNodename();

// Gets this executable's path name.
string GetMyPathname();

// Gets directory in which this executable is.
string GetMyDirectory();

// Get temporary directory to be used by gomacc and compiler_proxy.
// Temporary files, cache and an ipc socket file should be made under this
// directory for security.  Note that since an ipc socket file is created
// under this directory, the result of this function must be the same
// between gomacc and compiler_proxy.
//
// Note that we must ensure the directory is owned by the user who runs
// gomacc or compiler_proxy by CheckTempDirectory.
// (Once at the beginning of a program should be enough.)
string GetGomaTmpDir();

// Check temp directory is directory, owned only by self.
void CheckTempDirectory(const string& tmpdir);

// Get a directory name to store a crash dump.
string GetCrashDumpDirectory();

// Get a directory name to store a cache.
string GetCacheDirectory();

// Get current directory
string GetCurrentDirNameOrDie();

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_MYPATH_H_
