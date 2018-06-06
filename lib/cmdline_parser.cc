// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.



#include "cmdline_parser.h"

#include <ctype.h>

#include "basictypes.h"
#include "glog/logging.h"
#include "glog/stl_logging.h"
using std::string;

namespace devtools_goma {

// Parsing Command-Line Arguments (on posix for gcc, javac)
bool ParsePosixCommandLineToArgv(const string& cmdline,
                                 std::vector<string>* argv) {
  bool dquote = false;
  bool squote = false;
  bool backslash = false;
  bool in_arg = false;
  string arg = "";

  for (size_t i = 0; i < cmdline.size(); ++i) {
    char ch = cmdline[i];
    if (!in_arg) {
      if (isspace(ch)) continue;
      in_arg = true;
    }
    DCHECK(in_arg);
    if (isspace(ch) && !squote && !dquote && !backslash) {
      in_arg = false;
      argv->push_back(arg);
      arg = "";
      continue;
    }
    if (squote) {  // in single quote, anything will be saved as-is.
      if (ch == '\'') {
        squote = false;
        continue;
      }
      arg += ch;
      continue;
    }
    DCHECK(!squote);
    if (backslash) {
      backslash = false;
      if (ch == '\n')
        continue;
      // "a\b" -> a\b, "a\\b" -> a\b, "a\"b" -> a"b, "a\bc" -> abc
      if (dquote && ch != '\\' && ch != '"')
        arg += '\\';
      arg += ch;
      continue;
    }
    DCHECK(!backslash);
    if (ch == '\\') {  // backslash is available inside quote.
      backslash = true;
      continue;
    }
    if (dquote) {
      if (ch == '\"') {
        dquote = false;
        continue;
      }
      arg += ch;
      continue;
    }
    DCHECK(!dquote);
    if (ch == '\'') {
      squote = true;
      continue;
    }
    if (ch == '\"') {
      dquote = true;
      continue;
    }
    arg += ch;
  }
  if (in_arg) {
    argv->push_back(arg);
  }
  if (backslash) {
    LOG(ERROR) << "no next char for backslash: " << cmdline;
    return false;
  }
  if (squote || dquote) {
    LOG(ERROR) << "no closing quote: " << cmdline;
    return false;
  }
  return true;
}

// Parsing Command-Line Arguments (on Windows)
// http://msdn.microsoft.com/en-us/library/windows/desktop/17w5ykft(v=vs.85).aspx
bool ParseWinCommandLineToArgv(const string& cmdline,
                               std::vector<string>* argv) {
  size_t num_backslash = 0;
  bool arg_delimiter = false;
  bool in_quote = false;
  string arg = "";
  for (size_t i = 0; i < cmdline.size(); ++i) {
    char c = cmdline[i];
    switch (c) {
      case '\\':
        ++num_backslash;
        continue;
      case '"':
        if (num_backslash > 0) {
          // If an even number of backslashes is followed by a double
          // quotation mark, one backslash is placed in the argv array for
          // every pair of backslashes, and the double quotation mark is
          // interpreted as a string delimiter.
          for (size_t j = 0; j < num_backslash / 2; ++j) {
            arg += "\\";
          }
          // If an odd number of backslashes is followed by a double quotation
          // mark, one backslash is placed in the argv array for every pair of
          // backslashes, and the double quotation mark is "escaped" by the
          // remaining backslash, causing a literal double quotation mark (")
          // to be placed in argv
          if (num_backslash % 2 == 1) {
            arg += "\"";
          } else {
            in_quote = !in_quote;
          }
        } else {
          in_quote = !in_quote;
        }
        num_backslash = 0;
        continue;
      case ' ': case '\t': case '\r': case '\n':
        if (!in_quote)
          arg_delimiter = true;
        FALLTHROUGH_INTENDED;
      default:
        // Backslashes are interpreted literally, unless they immediately
        // precede a double quotation mark.
        if (num_backslash > 0) {
          for (size_t j = 0; j < num_backslash; ++j)
            arg += "\\";
          num_backslash = 0;
        }
        if (arg_delimiter) {
          // We cannot handle "" as an empty argument, but it might
          // never be a problem.
          if (!arg.empty())
            argv->push_back(arg);
          arg = "";
          arg_delimiter = false;
        } else {
          arg += c;
        }
    }
  }
  // Last argument.
  // Backslashes are interpreted literally, unless they immediately
  // precede a double quotation mark.
  if (num_backslash > 0) {
    for (size_t j = 0; j < num_backslash; ++j)
      arg += "\\";
  }
  if (!arg.empty())
    argv->push_back(arg);

  if (in_quote) {
    LOG(ERROR) << "no closing quote: " << cmdline;
    return false;
  }

  return true;
}

}  // namespace devtools_goma
