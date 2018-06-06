// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_THINLTO_IMPORT_PROCESSOR_H_
#define DEVTOOLS_GOMA_CLIENT_THINLTO_IMPORT_PROCESSOR_H_

#include <set>
#include <string>

#include "gcc_flags.h"

using std::string;

namespace devtools_goma {

class ThinLTOImportProcessor {
 public:
  // Get import files.
  static bool GetIncludeFiles(const string& thinlto_index,
                              const string& cwd,
                              std::set<string>* input_files);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_THINLTO_IMPORT_PROCESSOR_H_
