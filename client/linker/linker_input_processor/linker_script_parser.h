// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_LINKER_LINKER_INPUT_PROCESSOR_LINKER_SCRIPT_PARSER_H_
#define DEVTOOLS_GOMA_CLIENT_LINKER_LINKER_INPUT_PROCESSOR_LINKER_SCRIPT_PARSER_H_

#include <memory>
#include <string>
#include <vector>

#include "basictypes.h"
#include "content_cursor.h"

using std::string;

namespace devtools_goma {

// Linker script parser for Goma.
// It only supports commands dealing with files.
// http://sourceware.org/binutils/docs-2.17/ld/File-Commands.html#File-Commands
// Once Parse successfully done, it returns
//  searchdirs(): sarch directories
//  srartup(): startup object filename, if specified.
//  input(): input files in INPUT, GROUP or AS_NEEDED. may be "-lfile".
//  output(): output file, if specified.
class LinkerScriptParser {
 public:
  // Constructs a parser to read content.
  LinkerScriptParser(std::unique_ptr<Content> content,
                     string current_directory,
                     std::vector<string> searchdirs,
                     string sysroot);
  ~LinkerScriptParser();

  const std::vector<string>& searchdirs() const {
    return searchdirs_;
  }

  bool Parse();

  const string& startup() const {
    return startup_;
  }
  const std::vector<string>& inputs() const {
    return inputs_;
  }
  const string& output() const {
    return output_;
  }

 private:
  bool ParseUntil(const string& term_token);
  bool NextToken(string* token);
  bool GetToken(const string& token);
  bool ProcessFileList(bool accept_as_needed);
  bool ProcessFile(string* filename);
  bool ProcessInclude();
  bool ProcessInput();
  bool ProcessGroup();
  bool ProcessAsNeeded();
  bool ProcessOutput();
  bool ProcessSearchDir();
  bool ProcessStartup();
  bool FindFile(const string& filename, string* include_file);

  std::unique_ptr<ContentCursor> content_;
  const string current_directory_;
  std::vector<string> searchdirs_;
  const string sysroot_;

  string startup_;
  std::vector<string> inputs_;
  string output_;

  // provided for testing.
  static const char* fakeroot_;

  friend class LinkerScriptParserTest;

  DISALLOW_COPY_AND_ASSIGN(LinkerScriptParser);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_LINKER_LINKER_INPUT_PROCESSOR_LINKER_SCRIPT_PARSER_H_
