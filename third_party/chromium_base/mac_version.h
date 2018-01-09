// This is copied from chromium/src/base/mac/mac_util.h and modified for goma.
// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_MAC_MAC_VERSION_H_
#define BASE_MAC_MAC_VERSION_H_

namespace devtools_goma {

// Returns the running system's Mac OS X minor version. This is the |y| value
// in 10.y or 10.y.z.
int MacOSXMinorVersion();

}  // namespace devtools_goma

#endif  // BASE_MAC_MAC_VERSION_H_
