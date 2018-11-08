// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "openssl_engine_helper.h"

#include <windows.h>

#include <string>

#include "certs_resource.h"
#include "glog/logging.h"

using std::string;

namespace {

bool LoadTrustedRootCertsInResource(string* certs, int resource_id) {
  // Since we use the current process resource, HMODULE can be nullptr.
  HRSRC resource_info = FindResource(nullptr, MAKEINTRESOURCE(resource_id),
                                     RT_RCDATA);
  if (resource_info == nullptr) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Cannot find the root certificate resource.";
    return false;
  }
  HGLOBAL resource_handle = LoadResource(nullptr, resource_info);
  if (resource_handle == nullptr) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Cannot load the root certificate resource.";
    return false;
  }
  LPVOID resource = LockResource(resource_handle);
  if (resource == nullptr) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Cannot obtain pointer to the root certificate resource.";
    return false;
  }
  const DWORD resource_size = SizeofResource(nullptr, resource_info);
  if (resource_size == 0) {
    LOG_SYSRESULT(GetLastError());
    LOG(ERROR) << "Cannot get size of root certificate resource.";
    return false;
  }

  certs->assign(static_cast<const char*>(resource), resource_size);
  return true;
}

}  // anonymous namespace.

namespace devtools_goma {

bool GetTrustedRootCerts(string* certs) {
  string roots;
  if (!LoadTrustedRootCertsInResource(&roots, ROOT_CA_NAME)) {
    return false;
  }
  string dst_root;
  if (!LoadTrustedRootCertsInResource(&dst_root, DST_CA_NAME)) {
    return false;
  }
  *certs = roots + dst_root;
  return true;
}

}  // namespace devtools_goma
