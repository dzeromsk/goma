// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_BASE_FILESYSTEM_H_
#define DEVTOOLS_GOMA_BASE_FILESYSTEM_H_

#include "absl/strings/string_view.h"
#include "options.h"
#include "status.h"

namespace file {

// Returns ok if dirname and its children are successfully deleted.
::util::Status RecursivelyDelete(absl::string_view path,
                                 const Options& options);

::util::Status IsDirectory(absl::string_view path, const Options& options);

// Call this like CreateDir("/path/to/somewhere", file::CreationMode(0666)).
// creation mode of options will be ignored on Windows.
::util::Status CreateDir(absl::string_view path, const Options& options);

// Copy file
::util::Status Copy(absl::string_view from,
                    absl::string_view to,
                    const Options& options);

}  // namespace file

#endif  // DEVTOOLS_GOMA_BASE_FILESYSTEM_H_
