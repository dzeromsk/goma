// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Note:
// You SHOULD NOT use functions here in multi-threaded env.  (See spawner.h)

#ifndef DEVTOOLS_GOMA_CLIENT_SUBPROCESS_H_
#define DEVTOOLS_GOMA_CLIENT_SUBPROCESS_H_

#include <set>
#include <string>
#include <vector>

#include "util.h"

using std::string;

namespace devtools_goma {

struct FileStat;

#ifdef _WIN32

// execute program.
// returns -1 on start failure.
// return child process exit status from SpawnerWin.
int SpawnAndWait(const string& prog, const std::vector<string>& args,
                 const std::vector<string>& envs);

// execute program but automatically avoids executing gomacc.
// If |gomacc_filestat| == nullptr, this program won't avoid to execute gomacc.
// returns -1 on start failure.
// return child process exit status from SpawnerWin.
int SpawnAndWaitNonGomacc(const FileStat* gomacc_filestat,
                          const string& prog,
                          const std::vector<string>& args,
                          std::vector<string> envs);

#else

// execute program.
// If success, this function won't return.
// returns -1 on failure like execve system call.
//
// Don't use this in multi threaded env.  (See spawner.h)
int Execvpe(const string& prog, const std::vector<string>& args,
            const std::vector<string>& envs);

// execute program but automatically avoids executing gomacc.
// If |gomacc_filestat| == nullptr, this program won't avoid to execute gomacc.
//
// Don't use this in multi threaded env.  (See spawner.h)
int ExecvpeNonGomacc(const FileStat* gomacc_filestat,
                     const string& prog,
                     const std::vector<string>& args,
                     std::vector<string> envs);

#endif

#ifndef _WIN32
// Execute commandline by popen and read first 64kB of output into string.
// Exist code will be stored to |status|.
// If exit code is not zero and |status| == NULL, fatal error.
//
// Don't use this in multi threaded env.
string ReadCommandOutputByPopen(
    const string& prog, const std::vector<string>& argv,
    const std::vector<string>& env,
    const string& cwd, CommandOutputOption option, int32_t* status);

// The caller must fork before calling this.
// If |stderr_filename| is not empty, it redirects the spawning child's
// stderr output to the file.
// If |pid_record_fd| is >= 0, it writes out the spawning child's pid (i.e.
// grandchild's pid to the caller's parent) to the fd and closes it.
// All fds except stdin/stdout/stderr and in |preserve_fds| will be closed.
//
// Don't use this in multi threaded env.
void Daemonize(const string& stderr_filename, int pid_record_fd,
               const std::set<int>& preserve_fds);
#else
// Execute commandline by spawner_win and read output into string.
// Exist code will be stored to |status|.
// If exit code is not zero and |status| == NULL, fatal error.
//
// Don't use this in multi threaded env.  (See spawner.h)
string ReadCommandOutputByRedirector(const string& prog,
    const std::vector<string>& argv, const std::vector<string>& env,
    const string& cwd, CommandOutputOption option, int32_t* status);
#endif

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SUBPROCESS_H_
