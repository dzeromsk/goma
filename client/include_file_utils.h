// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_INCLUDE_FILE_UTILS_H_
#define DEVTOOLS_GOMA_CLIENT_INCLUDE_FILE_UTILS_H_

#include <string>
#include <utility>
#include <vector>

namespace devtools_goma {

extern const char* GOMA_GCH_SUFFIX;

bool CreateSubframeworkIncludeFilename(
    const std::string& fwdir, const std::string& current_directory,
    const std::string& include_name, std::string* filename);

bool ReadHeaderMapContent(
    const std::string& hmap_filename,
    std::vector<std::pair<std::string, std::string>>* entries);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_INCLUDE_FILE_UTILS_H_
