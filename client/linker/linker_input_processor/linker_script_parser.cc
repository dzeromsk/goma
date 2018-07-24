// Copyright 2011 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "linker_script_parser.h"

#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <glog/logging.h>
#include <glog/stl_logging.h>

#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "content.h"
#include "path.h"
#include "path_util.h"

#ifdef _WIN32
#include "posix_helper_win.h"
#endif

namespace devtools_goma {

const char* LinkerScriptParser::fakeroot_ = "";

LinkerScriptParser::LinkerScriptParser(std::unique_ptr<Content> content,
                                       string current_directory,
                                       std::vector<string> searchdirs,
                                       string sysroot)
    : content_(new ContentCursor(std::move(content))),
      current_directory_(std::move(current_directory)),
      searchdirs_(std::move(searchdirs)),
      sysroot_(std::move(sysroot)) {}

LinkerScriptParser::~LinkerScriptParser() {
}

bool LinkerScriptParser::Parse() {
  return ParseUntil("");
}

bool LinkerScriptParser::ParseUntil(const string& term_token) {
  string token;
  while (NextToken(&token)) {
    if (!term_token.empty() && token == term_token) {
      return true;
    }
    if (token == "INCLUDE") {
      if (!ProcessInclude())
        return false;
    } else if (token == "INPUT") {
      if (!ProcessInput())
        return false;
    } else if (token == "GROUP") {
      if (!ProcessGroup())
        return false;
    } else if (token == "OUTPUT") {
      if (!ProcessOutput())
        return false;
    } else if (token == "SEARCH_DIR") {
      if (!ProcessSearchDir())
        return false;
    } else if (token == "STARTUP") {
      if (!ProcessStartup())
        return false;
    } else if (token == "(") {
      VLOG(1) << "Open (";
      if (!ParseUntil(")")) {
        LOG(WARNING) << "Unbalanced ()?";
        return false;
      }
      VLOG(1) << "Close )";
    } else if (token == "{") {
      VLOG(1) << "Open {";
      if (!ParseUntil("}")) {
        LOG(WARNING) << "Unbalanced {}?";
        return false;
      }
      VLOG(1) << "Close }";
    } else {
      VLOG(1) << "Ignore token:" << token;
    }
  }
  return term_token.empty();
}

bool LinkerScriptParser::NextToken(string* token) {
  const char* p = nullptr;
  int ch = EOF;
  bool is_token_start = false;
  while (!is_token_start) {
    p = content_->cur();
    ch = content_->GetChar();
    VLOG(3) << "token? at " << p - content_->buf()
            << " '" << static_cast<char>(*p) << "'";
    switch (ch) {
      case EOF:
        VLOG(1) << "EOF";
        return false;
      case '/':
        if (*content_->cur() == '*') {
          while ((ch = content_->GetChar()) != EOF) {
            if (!content_->SkipUntil('*'))
              return false;
            content_->Advance(1);
            if (*content_->cur() == '/') {
              ch = content_->GetChar();
              break;
            }
          }
          VLOG(2) << "Skip comment:" << string(p, content_->cur() - p);
          continue;
        } else if (*content_->cur() == '=') {
          ch = content_->GetChar();
          *token = string(p, content_->cur() - p);
          VLOG(2) << "Token(op) '" << *token << "'";
          return true;
        }
        is_token_start = true;
        break;
      case ' ': case '\t': case '\n': case '\r': case ',': case ';':
        VLOG(2) << "Skip '" << static_cast<char>(ch) << "'";
        continue;
      case '(': case ')': case '{': case '}': case ':': case '?':
      case '~': case '%':
        *token = string(1, static_cast<char>(ch));
        VLOG(2) << "Token(char) '" << *token << "'";
        return true;

      case '=': case '!': case '+': case '-': case '*':
        if (*content_->cur() == '=') {
          ch = content_->GetChar();
          *token = string(p, content_->cur() - p);
          VLOG(2) << "Token(op) '" << *token << "'";
          return true;
        }
        *token = string(1, static_cast<char>(ch));
        VLOG(2) << "Token(char) '" << *token << "'";
        return true;

      case '&': case '|':
        if (*content_->cur() == '=' || *content_->cur() == ch) {
          ch = content_->GetChar();
          *token = string(p, content_->cur() - p);
          VLOG(2) << "Token(op) '" << *token << "'";
          return true;
        }
        *token = string(1, static_cast<char>(ch));
        VLOG(2) << "Token(char) '" << *token << "'";
        return true;

      case '<': case '>':
        if (*content_->cur() == ch)
          ch = content_->GetChar();
        if (*content_->cur() == '=' || *content_->cur() == ch) {
          ch = content_->GetChar();
          *token = string(p, content_->cur() - p);
          VLOG(2) << "Token(op) '" << *token << "'";
          return true;
        }
        *token = string(p, content_->cur() - p);
        VLOG(1) << "Token(op) '" << *token << "'";
        return true;

      case '"':
        p = content_->cur();
        if (!content_->SkipUntil('"'))
          return false;
        content_->Advance(1);
        *token = string(p, content_->cur() - p - 1);
        VLOG(2) << "Token(quoted-string) " << *token;
        return true;

      default:
        is_token_start = true;
        break;
    }
  }
  VLOG(3) << "token_start at " << p - content_->buf()
          << " '" << static_cast<char>(*p) << "'";
  const char* token_start = p;
  for (;;) {
    p = content_->cur();
    if (p == content_->buf_end()) {
      *token = string(token_start, p - token_start - 1);
      VLOG(2) << "Token(EOF) " << *token;
      return true;
    }
    switch (*p) {
      case ' ': case '\t': case '\n': case '\r': case ',': case ';':
      case '(': case ')': case '{': case '}':
      case '"':
        // end od token.
        *token = string(token_start, p - token_start);
        VLOG(2) << "Token '" << *token << "'";
        return true;
      default:
        // '/' or other char might be used in filename.
        ch = content_->GetChar();
    }
  }
}

bool LinkerScriptParser::GetToken(const string& token) {
  VLOG(1) << "Expect token " << token;
  string next_token;
  if (!NextToken(&next_token)) {
    LOG(WARNING) << "Expected " << token << ", but got " << next_token;
    return false;
  }
  return token == next_token;
}

bool LinkerScriptParser::ProcessFileList(bool accept_as_needed) {
  VLOG(1) << "FileList as_needed=" << accept_as_needed;
  if (!GetToken("("))
    return false;
  string token;
  while (NextToken(&token)) {
    if (token == ")") {
      return true;
    }
    if (token == "AS_NEEDED") {
      if (accept_as_needed) {
        if (!ProcessAsNeeded())
          return false;
      } else {
        return false;
      }
    } else if (token == "(" || token == "{" || token == "}") {
      LOG(WARNING) << "Unexpected token " << token << " in file list.";
    } else {
      VLOG(1) << "Add to input:" << token;
      if (token[0] == '/' && !sysroot_.empty() &&
          HasPrefixDir(current_directory_, sysroot_)) {
        token = file::JoinPath(sysroot_, token.substr(1));
      }
      string input_file;
      if (FindFile(token, &input_file)) {
        inputs_.push_back(input_file);
      } else {
        LOG(WARNING) << "cannot find full path of the file: " << token;
      }
    }
  }
  return false;
}

bool LinkerScriptParser::ProcessFile(string* filename) {
  VLOG(1) << "File";
  if (!GetToken("("))
    return false;
  if (!NextToken(filename))
    return false;
  return GetToken(")");
}

// INCLUDE filename
bool LinkerScriptParser::ProcessInclude() {
  string filename;
  if (!NextToken(&filename))
    return false;
  string include_file;
  if (!FindFile(filename, &include_file)) {
    LOG(ERROR) << "file:" << filename << " not found in searchdirs:"
               << searchdirs_;
    return false;
  }
  LinkerScriptParser parser(
      Content::CreateFromFile(include_file),
      current_directory_,
      searchdirs_,
      sysroot_);
  if (!parser.Parse()) {
    LOG(ERROR) << "INCLUDE " << filename << "(" << include_file << ") "
               << " parse error";
    return false;
  }
  if (!parser.startup().empty())
    startup_ = parser.startup();
  copy(parser.inputs().begin(), parser.inputs().end(),
       back_inserter(inputs_));
  if (!parser.output().empty())
    output_ = parser.output();
  return true;
}

// INPUT(file file ...)
bool LinkerScriptParser::ProcessInput() {
  VLOG(1) << "Process INPUT";
  return ProcessFileList(true);
}

// GROUP(file file ...)
bool LinkerScriptParser::ProcessGroup() {
  VLOG(1) << "Process GROUP";
  return ProcessFileList(true);
}

// AS_NEEDED(file file ...) only inside of the INPUT or GROUP commands.
bool LinkerScriptParser::ProcessAsNeeded() {
  VLOG(1) << "Process AS_NEEDED";
  return ProcessFileList(false);
}

// OUTPUT(filename)
bool LinkerScriptParser::ProcessOutput() {
  VLOG(1) << "Process OUTPUT";
  return ProcessFile(&output_);
}

// SEARCH_DIR(path) => -Lpath
bool LinkerScriptParser::ProcessSearchDir() {
  VLOG(1) << "Process SEARCH_DIR";
  string path;
  if (!ProcessFile(&path))
    return false;
  searchdirs_.push_back(path);
  return true;
}

// STARTUP(filename)
bool LinkerScriptParser::ProcessStartup() {
  VLOG(1) << "Process STARTUP";
  return ProcessFile(&startup_);
}

bool LinkerScriptParser::FindFile(const string& filename,
                                  string* include_file) {
  string resolved_filename = fakeroot_ +
      file::JoinPathRespectAbsolute(current_directory_, filename);
  if (access(resolved_filename.c_str(), R_OK) == 0) {
    *include_file = resolved_filename.substr(strlen(fakeroot_));
    return true;
  }
  for (const auto& dir : searchdirs_) {
    resolved_filename = fakeroot_ +
        file::JoinPathRespectAbsolute(
            file::JoinPathRespectAbsolute(current_directory_, dir),
            filename);
    if (access(resolved_filename.c_str(), R_OK) == 0) {
      *include_file = resolved_filename.substr(strlen(fakeroot_));
      return true;
    }
  }
  return false;
}

}  // namespace devtools_goma
