// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "gomacc_argv.h"

#include <string.h>

#include <fstream>
#include <sstream>

#include "absl/strings/match.h"
#include "compiler_flags.h"
#include "path.h"

namespace devtools_goma {

static const char* kGomaVerifyCommandFlag = "--goma-verify-command";

bool BuildGomaccArgv(int orig_argc, const char* orig_argv[],
                     std::vector<string>* args,
                     bool* masquerade_mode,
                     string* verify_command,
                     string* local_command_path) {
  int argv0 = -1;
  const string progname = string(file::Basename(orig_argv[0]));
  for (int i = 0; i < orig_argc; i++) {
    if (absl::StartsWith(orig_argv[i], kGomaVerifyCommandFlag)) {
      // --goma-veirfy-command is useful for end-to-end test.
      // It always sends a compile request from compiler_proxy to remote server,
      // ignores cache, and checks compiler version between local and remote.
      // It also takes a parameter:
      //  "none": doesn't check compiler version.
      //  "version": check version string only
      //  "checksum": check binary hash only
      //  "all": check "version" and "checksum".
      if (strcmp(orig_argv[i], kGomaVerifyCommandFlag) == 0) {
        *verify_command = "all";
      } else if (orig_argv[i][strlen(kGomaVerifyCommandFlag)] == '=') {
        *verify_command = orig_argv[i] + strlen(kGomaVerifyCommandFlag) + 1;
      }
      if (*verify_command != "version" && *verify_command != "checksum" &&
          *verify_command != "all" && *verify_command != "none") {
        fprintf(stderr, "Wrong --goma-verify-command: %s\n",
                verify_command->c_str());
        fprintf(stderr,
                " use \"version\", \"checksum\", \"all\" or \"none\".\n");
        return false;
      }
      continue;
    }
    if (*orig_argv[i] == '-') {
      // option found without having gcc or g++ as command name.
      break;
#ifdef _WIN32
    } else if (*orig_argv[i] == '/') {
      break;
#endif
    }
    // found command name.
    const absl::string_view p = file::Basename(orig_argv[i]);
    if (p == "gomacc"
#ifdef _WIN32
        || p == "gomacc.exe"
#endif
        ) {
      continue;
    }
    argv0 = i;
    if (i != 0 && p != orig_argv[i]) {
      // If this was not the first argument (i.e. symlinked name),
      // and argv[i] is not basename, then we'll see this as local command path.
      *local_command_path = orig_argv[i];
    }
    break;
  }
  if (argv0 < 0)
    return false;
  *masquerade_mode = argv0 == 0;
  orig_argc -= argv0;
  for (int i = 0; i < orig_argc; i++) {
    // if masqueraded mode, use basename of argv[0].
    if (i == 0 && *masquerade_mode)
      args->push_back(progname);
    else
      args->push_back(orig_argv[i + argv0]);
  }
  return true;
}

#ifdef _WIN32

void FanOutArgsByInput(
  const std::vector<string>& args,
  const std::set<string>& input_filenames,
  std::vector<string>* args_no_input) {
  for (size_t i = 1; i < args.size(); ++i) {
    if (input_filenames.count(args[i]))
      continue;
    args_no_input->push_back(args[i]);
  }
}

string BuildArgsForInput(
    const std::vector<string>& args_no_input,
    const string& input_filename) {
  std::ostringstream rsp;
  for (const auto& arg : args_no_input) {
    rsp << EscapeWinArg(arg) << " ";
  }
  // assume input_filename doesn't end with \.
  // TODO: quote input_filename correctly.
  rsp << "\"" << input_filename << "\"";
  return rsp.str();
}

string EscapeWinArg(const string& arg) {
  std::stringstream ss;
  ss << '"';
  for (size_t i = 0; i < arg.size(); ++i) {
    char c = arg[i];
    switch (c) {
      case '"':  // " -> \"
        ss << '\\' << '"';
        break;
      case '\\':
        if (i + 1 == arg.size()) {
          // \ at the end of string. => "...\\"
          ss << '\\';
        } else if (arg[i + 1] == '"') {
          // \ before " => ..\\\"..
          ss << '\\';
        }  // otherwise, backslashes are interpreted literally.

        FALLTHROUGH_INTENDED;
      default:
        ss << c;
        break;
    }
  }
  ss << '"';
  return ss.str();
}

#endif  // _WIN32

}  // namespace devtools_goma
