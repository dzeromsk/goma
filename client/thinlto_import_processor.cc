// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thinlto_import_processor.h"

#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "path.h"

namespace devtools_goma {

// static
bool ThinLTOImportProcessor::GetIncludeFiles(const string& thinlto_index,
                                             const string& cwd,
                                             std::set<string>* input_files) {
  static const char kIndexFileSuffix[] = ".thinlto.bc";
  static const char kImportsFileSuffix[] = ".imports";

  if (!absl::EndsWith(thinlto_index, kIndexFileSuffix)) {
    LOG(WARNING) << "thinlto index has unexpected suffix."
                 << " thinlto_index=" << thinlto_index;
    return false;
  }

  // .imports file represents which file is needed to execute ThinLTO backend
  // phase.  We need to upload files listed there.
  // See:
  // https://github.com/llvm-mirror/llvm/blob/71e93dfc4b97a3291302ad83f82767a4ebd0ae72/tools/gold/gold-plugin.cpp#L158
  const string imports_file = file::JoinPathRespectAbsolute(
      cwd, thinlto_index.substr(
                0, thinlto_index.size() - strlen(kIndexFileSuffix)) +
                kImportsFileSuffix);

  string contents;
  if (!ReadFileToString(imports_file, &contents)) {
    LOG(WARNING) << "Failed to read .imports file."
                 << " imports_file=" << imports_file;
    return false;
  }

  for (auto&& line :
       absl::StrSplit(contents, absl::ByAnyChar("\r\n"), absl::SkipEmpty())) {
    input_files->insert(string(line));
  }

  return true;
}

}  // namespace devtools_goma
