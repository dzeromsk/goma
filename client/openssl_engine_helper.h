// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_OPENSSL_ENGINE_HELPER_H_
#define DEVTOOLS_GOMA_CLIENT_OPENSSL_ENGINE_HELPER_H_

#include <string>

using std::string;

namespace devtools_goma {

// Gets trusted root certificates (PEM).
// Returns true if succeeded.  Otherwise, false.
bool GetTrustedRootCerts(string* certs);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_OPENSSL_ENGINE_HELPER_H_
