// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpp_parser.h"

#include <memory>

#include "cpp_directive_parser.h"
#include "include_item.h"
#include "ioutil.h"
#include "path.h"
#include "path_resolver.h"

using devtools_goma::Content;
using devtools_goma::CppDirectiveList;
using devtools_goma::CppDirectiveParser;
using devtools_goma::CppParser;
using devtools_goma::GetBaseDir;
using devtools_goma::GetCurrentDirNameOrDie;
using devtools_goma::IncludeItem;
using devtools_goma::PathResolver;
using devtools_goma::SharedCppDirectives;

class TestIncludeObserver : public CppParser::IncludeObserver {
 public:
  bool HandleInclude(
      const string& path,
      const string& current_directory ALLOW_UNUSED,
      const string& current_filepath ALLOW_UNUSED,
      char quote_char ALLOW_UNUSED,  // '"' or '<'
      int include_dir_index ALLOW_UNUSED) override {
    if (quote_char == '<' &&
        include_dir_index > CppParser::kIncludeDirIndexStarting) {
      std::cout << "#INCLUDE_NEXT ";
    } else {
      std::cout << "#INCLUDE ";
    }
    char close_quote_char = (quote_char == '<') ? '>' : quote_char;
    std::cout << quote_char << path << close_quote_char << std::endl;
#ifdef _WIN32
    UNREFERENCED_PARAMETER(current_directory);
    UNREFERENCED_PARAMETER(current_filepath);
#endif
    return true;
  }

  bool HasInclude(
      const string& path ALLOW_UNUSED,
      const string& current_directory ALLOW_UNUSED,
      const string& current_filepath ALLOW_UNUSED,
      char quote_char ALLOW_UNUSED,  // '"' or '<'
      int include_dir_index ALLOW_UNUSED) override {
#ifdef _WIN32
    UNREFERENCED_PARAMETER(path);
    UNREFERENCED_PARAMETER(current_directory);
    UNREFERENCED_PARAMETER(current_filepath);
    UNREFERENCED_PARAMETER(quote_char);
    UNREFERENCED_PARAMETER(include_dir_index);
#endif
    return true;
  }
};

class TestErrorObserver : public CppParser::ErrorObserver {
 public:
  void HandleError(const string& error) override {
    LOG(WARNING) << error;
  }
};

static bool TryAddFileInput(CppParser* parser, const string& filepath,
                            int include_dir_index) {
  std::unique_ptr<Content> content(Content::CreateFromFile(filepath));
  if (!content) {
    return false;
  }

  SharedCppDirectives directives(
      CppDirectiveParser::ParseFromContent(*content));
  if (!directives) {
    return false;
  }

  string directory;
  GetBaseDir(filepath, &directory);
  parser->AddFileInput(IncludeItem(directives, ""), filepath, directory,
                       include_dir_index);
  return true;
}

static std::pair<string, string> GetMacroArg(const char* arg) {
  string macro(arg);
  size_t found = macro.find('=');
  if (found == string::npos) {
    return std::make_pair(macro, "");
  }
  const string& key = macro.substr(0, found);
  const string& value = macro.substr(found + 1, macro.size() - (found + 1));
  return std::make_pair(key, value);
}

int main(int argc, char *argv[]) {
  int ac = 1;
  std::vector<std::pair<string, string>> arg_macros;
  for (; ac < argc; ++ac) {
    if (strncmp(argv[ac], "-D", 2) == 0) {
      if (strlen(argv[ac]) > 2) {
        arg_macros.push_back(GetMacroArg(&argv[ac][2]));
      } else if (ac + 1 < argc) {
        arg_macros.push_back(GetMacroArg(argv[++ac]));
      }
      continue;
    }
    break;
  }

  if (ac >= argc) {
    std::cerr << argv[0] << " [-D<macro> ...] path" << std::endl;
    std::cerr << "e.g.: " << argv[0] << " -D'S(x)=<lib##x.h>' tmp.c"
              << std::endl;
    exit(1);
  }

  const string cwd = GetCurrentDirNameOrDie();

  PathResolver path_resolver;

  string path = file::JoinPathRespectAbsolute(cwd, argv[ac]);
  path = path_resolver.ResolvePath(path);

  std::cout << std::endl << "===== Directives =====" << std::endl;
  {
    std::unique_ptr<Content> content(Content::CreateFromFile(path));
    if (!content) {
      std::cerr << "failed to read: " << path << std::endl;
      exit(1);
    }

    SharedCppDirectives directives(
        CppDirectiveParser::ParseFromContent(*content));
    if (!directives) {
      std::cout << "failed to parse: " << path << std::endl;
      exit(1);
    }

    for (const auto& d : *directives) {
      std::cout << d->DebugString() << std::endl;
    }
  }

  {
    CppParser parser;
    TestIncludeObserver include_observer;
    TestErrorObserver error_observer;
    TryAddFileInput(&parser, path, CppParser::kCurrentDirIncludeDirIndex);
    parser.set_include_observer(&include_observer);
    parser.set_error_observer(&error_observer);

    for (const auto& arg_macro : arg_macros) {
      parser.AddMacroByString(arg_macro.first, arg_macro.second);
    }

    std::cout << std::endl << "===== Includes =====" << std::endl;
    parser.ProcessDirectives();

    std::cout << std::endl << "===== Macros =====" << std::endl;
    std::cout << parser.DumpMacros();
  }
}
