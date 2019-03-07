// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "goma_file_dump.h"

#include "compiler_specific.h"
#include "file_helper.h"
#include "glog/logging.h"
#include "goma_data_util.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

FileServiceDumpClient::FileServiceDumpClient()
    : req_(new StoreFileReq) {
}

FileServiceDumpClient::~FileServiceDumpClient() {
}

bool FileServiceDumpClient::StoreFile(
    const StoreFileReq* req, StoreFileResp* resp) {
  for (const auto& b : req->blob()) {
    FileBlob* blob = req_->add_blob();
    *blob = b;
    resp->add_hash_key(ComputeFileBlobHashKey(*blob));
  }
  return true;
}

bool FileServiceDumpClient::Dump(const string& filename) const {
  if (req_->blob_size() == 0)
    return true;
  string s;
  req_->SerializeToString(&s);
  return WriteStringToFile(s, filename);
}

}  // namespace devtools_goma
