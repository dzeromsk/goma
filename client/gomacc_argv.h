// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMACC_ARGV_H_
#define DEVTOOLS_GOMA_CLIENT_GOMACC_ARGV_H_

#include <set>
#include <string>
#include <vector>

using std::string;

namespace devtools_goma {

// Builds "args" for ExecReq from gomacc argv.
// If masqueraded, "masquerade_mode" set to true.
// If --goma-verify-command is specified, set the value to "verify_command".
// Possible value would be "none", "version", "checksum" and "all".
// If prepend mode with path/to/compiler, set the path to "local_command_path".
// Returns true if successful, returns false otherwise.
//
// - masqueraded: (e.g. ln -sf gomacc gcc, or copy gomacc.exe cl.exe)
//  - masquerade in path.e.g. argv[0] = "gcc" or argv[0] = "cl.exe"
//    for example
//      "gcc", "-c", "hello.c"
//      "cl.exe", "/c", "hello.c"
//    send original argv to compiler_proxy
//    =>
//       "gcc", "-c", "hello.c",
//            local_path=$PATH, gomacc_path=/gomadir/gcc
//       "cl.exe", "/c", "hello.c"
//            local_path=%PATH%, gomacc_path=c:\gomadir\cl.exe
//    local_compiler_path from command name and local_path.
//    (after a dir where gomacc masquerade exists).
//  - masquerade full path. e.g. argv[0] = "/gomadir/gcc" or so.
//    for example
//      "/gomadir/gcc", "/c", "hello.c"
//      "c:\gomadir\cl.exe", "/c", "hello.c"
//    use basename of argv[0] to send compiler_proxy.
//    =>
//       "gcc", "-c", "hello.c"
//            local_path=$PATH, gomacc_path=/gomadir/gcc
//       "cl.exe", "/c", "hello.c"
//            local_path=%PATH%, gomacc_path=c:\gomadir\cl.exe
//    local_compiler_path from command name and local_path.
//    maybe, local_path should not contain gomadir.
//
// - prepended: (e.g. gomacc gcc or gomacc.exe cl.exe)
//  - prepended to no full path of compiler
//    (gomacc may or may not be full path)
//    for example
//       "gomacc", "gcc", "-c", "hello.c"
//       "gomacc.exe", "cl.exe", "/c", "hello.c"
//    =>
//       "gcc", "-c", "hello.c"
//           local_path=$PATH, gomacc_path=/gomadir/gomacc
//       "cl.exe", "/c", "hello.c"
//           local_path=%PATH%, gomacc_path=c:\gomadir\gomacc.exe
//    local_compiler_path from command name and local_path.
//    local_path should not contain gomadir.
//  - prepended to full path or current relative path of compiler
//    (gomacc may or may not be full path)
//    for example
//       "gomacc", "/usr/bin/gcc", "-c", "hello.c"
//       "gomacc.exe", "c:\vc\bin\cl.exe", "/c", "hello.c"
//    =>
//       "/usr/bin/gcc", "-c", "hello.c"
//          local_path=$PATH, gomacc_path=/gomadir/gomacc
//          local_compiler_path=/usr/bin/gcc
//       "c:\vc\bin\cl.exe", "/c", "hello.c"
//          local_path=%PATH%, gomacc_path=c:\gomadir\gomacc.exe
//          local_compiler_path=c:\vc\bin\cl.exe
//    local_compiler_path if the full path of compiler.
bool BuildGomaccArgv(int orig_argc, const char* orig_argv[],
                     std::vector<string>* args,
                     bool* masquerade_mode,
                     string* verify_command,
                     string* local_command_path);

#ifdef _WIN32
// Used for GOMA_FAN_OUT_EXEC_REQ=true (under devenv or msbuild).

// Fans out "args" for each input filename, and sets command line args
// for each input filename in "args_no_input".
// Note that "args_no_input" doesn't have argv0.
void FanOutArgsByInput(
    const std::vector<string>& args,
    const std::set<string>& input_filenames,
    std::vector<string>* args_no_input);

// Creates command line per input file as
//   args_no_input...  input_filename
// The returned value is expected to be written in rsp_file, and
// used as "cl @rsp_file".
string BuildArgsForInput(
    const std::vector<string>& args_no_input,
    const string& input_filename);

// Escape arg string for Windows.
// http://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft(v=vs.85).aspx
string EscapeWinArg(const string& arg);

#endif

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMACC_ARGV_H_
