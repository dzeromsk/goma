// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "openssl_engine_helper.h"

#include "absl/strings/str_cat.h"

#include "DST_Root_CA_X3.h"
#include "roots.h"

namespace devtools_goma {

bool GetTrustedRootCerts(string* certs) {
  *certs = absl::StrCat(
      absl::string_view(certs_roots_pem_start, certs_roots_pem_size),
      absl::string_view(certs_DST_Root_CA_X3_pem_start,
                        certs_DST_Root_CA_X3_pem_size));
  return true;
}

}  // namespace devtools_goma
