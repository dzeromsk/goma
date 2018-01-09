// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_TIMESTAMP_H_
#define DEVTOOLS_GOMA_CLIENT_TIMESTAMP_H_

namespace devtools_goma {

typedef long long millitime_t;
millitime_t GetCurrentTimestampMs();

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_TIMESTAMP_H_
