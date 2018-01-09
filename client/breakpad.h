// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_BREAKPAD_H_
#define DEVTOOLS_GOMA_CLIENT_BREAKPAD_H_

#include <string>

namespace devtools_goma {
void InitCrashReporter(const std::string& dump_output_dir);
bool IsCrashReporterEnabled();
}  // namespace devtools_goma
#endif  // DEVTOOLS_GOMA_CLIENT_BREAKPAD_H_
