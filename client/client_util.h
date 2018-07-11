// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CLIENT_UTIL_H_
#define DEVTOOLS_GOMA_CLIENT_CLIENT_UTIL_H_

#include <string>

#include "file_stat.h"

using std::string;

namespace devtools_goma {

// Returns true if |candidate_path| (at |cwd| with PATH=|path|) is gomacc.
// Note: this is usually used to confirm the |candidate_path| is not gomacc.
// Note: You MUST call InstallReadCommandOuptutFunc beforehand.
bool IsGomacc(const string& candidate_path,
              const string& path,
              const string& pathext,
              const string& cwd);

// Find a real path name of |cmd| from |path_env|.
// It avoids to choose the file having same FileStat with |gomacc_filestat|.
// It returns true on success, and |local_compiler_path| (real compiler path)
// and |no_goma_path| (PATH env. without gomacc) are set.
// On Windows, |pathext_env| is used as PATHEXT parameter.
// Other platform should set empty |pathext_env| or fatal error.
// |cwd| represents current working directory.
// If returned |local_compiler_path| depends on the current working directory,
// |is_in_relative_path| become true.
// Note: you can use NULL to |no_goma_path_env| and |is_in_relative_path|
// if you do not need them.
// Note: You MUST call InstallReadCommandOuptutFunc beforehand if you
//       use gomacc_filestat.
bool GetRealExecutablePath(const FileStat* gomacc_filestat,
                           const string& cmd,
                           const string& cwd,
                           const string& path_env,
                           const string& pathext_env,
                           string* local_compiler_path,
                           string* no_goma_path_env,
                           bool* is_in_relative_path);

#ifdef _WIN32
// Resolves path extension of |cmd| using PATHEXT environment given with
// |pathext_env|.  If |cmd| is not an absolute path, it is automatically
// converted to an absolute path using |cwd|.
string ResolveExtension(const string& cmd,
                        const string& pathext_env,
                        const string& cwd);
#endif

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CLIENT_UTIL_H_
