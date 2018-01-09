// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "openssl_engine_helper.h"
#include "roots.h"

namespace devtools_goma {

bool GetTrustedRootCerts(string* certs) {
  certs->assign(certs_roots_pem_start, certs_roots_pem_size);
  return true;
}

}  // namespace devtools_goma
