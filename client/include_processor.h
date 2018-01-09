// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_INCLUDE_PROCESSOR_H_
#define DEVTOOLS_GOMA_CLIENT_INCLUDE_PROCESSOR_H_

#include <map>
#include <set>
#include <string>

#include "basictypes.h"
#include "cpp_parser.h"
#include "file_id_cache.h"

using std::string;

namespace devtools_goma {

class CompilerFlags;
class CompilerInfo;
class Content;

class IncludeProcessor {
 public:
  IncludeProcessor() {}
  ~IncludeProcessor() {}

  // Enumerates all include files. When FileIds are created for them,
  // we cache them in |file_id_cache| so that we can reuse them later,
  // because creating FileId is so slow especially on Windows.
  bool GetIncludeFiles(const string& filename,
                       const string& current_directory,
                       const CompilerFlags& compiler_flags,
                       const CompilerInfo& compiler_info,
                       std::set<string>* include_files,
                       FileIdCache* file_id_cache);

  const CppParser* cpp_parser() const { return &cpp_parser_; }

  int total_files() const;
  int skipped_files() const;

 private:
  CppParser cpp_parser_;

  // [macro, cwd] -> is_include_next
  std::map<std::pair<string, string>, bool> delayed_macro_includes_;

  friend class IncludeProcessorTest;

  DISALLOW_COPY_AND_ASSIGN(IncludeProcessor);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_INCLUDE_PROCESSOR_H_
