// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_HTTP_INIT_H_
#define DEVTOOLS_GOMA_CLIENT_HTTP_INIT_H_

#include "http.h"

namespace devtools_goma {

void InitHttpClientOptions(HttpClient::Options* http_options);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_HTTP_INIT_H_
