// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_SETTINGS_H_
#define DEVTOOLS_GOMA_CLIENT_SETTINGS_H_

#include <string>

namespace devtools_goma {

class WorkerThreadManager;

std::string ApplySettings(const std::string& settings_server,
                          const std::string& expect_settings,
                          WorkerThreadManager* wm);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SETTINGS_H_
