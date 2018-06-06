// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CPP_INCLUDE_PROCESSOR_H_
#define DEVTOOLS_GOMA_CLIENT_CPP_INCLUDE_PROCESSOR_H_

#include <map>
#include <set>
#include <string>

#include "basictypes.h"
#include "cpp_parser.h"
#include "file_stat_cache.h"
#include "include_file_finder.h"

using std::string;

namespace devtools_goma {

class CompilerFlags;
class CompilerInfo;
class Content;

class CppIncludeProcessor {
 public:
  CppIncludeProcessor() {}
  ~CppIncludeProcessor() {}

  // Enumerates all include files. When FileStats are created for them,
  // we cache them in |file_stat_cache| so that we can reuse them later,
  // because creating FileStat is so slow especially on Windows.
  bool GetIncludeFiles(const string& filename,
                       const string& current_directory,
                       const CompilerFlags& compiler_flags,
                       const CompilerInfo& compiler_info,
                       std::set<string>* include_files,
                       FileStatCache* file_stat_cache);

  const CppParser* cpp_parser() const { return &cpp_parser_; }

  int total_files() const;
  int skipped_files() const;

 private:
  // Returns a vector of tuple<filepath, dir_index>.
  std::vector<std::pair<string, int>> CalculateRootIncludesWithIncludeDirIndex(
      const std::vector<string>& root_includes,
      const string& current_directory,
      const CompilerFlags& compiler_flags,
      IncludeFileFinder* include_file_finder,
      std::set<string>* include_files);

  CppParser cpp_parser_;

  friend class CppIncludeProcessorTest;

  DISALLOW_COPY_AND_ASSIGN(CppIncludeProcessor);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CPP_INCLUDE_PROCESSOR_H_
