// Copyright 2012 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_GOMA_FILE_DUMP_H_
#define DEVTOOLS_GOMA_CLIENT_GOMA_FILE_DUMP_H_

#include <memory>
#include <string>

#include "goma_file.h"

namespace devtools_goma {

class FileServiceDumpClient : public FileServiceClient {
 public:
  FileServiceDumpClient();
  ~FileServiceDumpClient() override;

  // No async support.
  std::unique_ptr<AsyncTask<StoreFileReq, StoreFileResp>>
      NewAsyncStoreFileTask() override {
    return nullptr;
  }
  std::unique_ptr<AsyncTask<LookupFileReq, LookupFileResp>>
      NewAsyncLookupFileTask() override {
    return nullptr;
  }

  // Records all StoreFileReqs.  Always success.
  bool StoreFile(const StoreFileReq* req, StoreFileResp* resp) override;
  // No lookup support
  bool LookupFile(const LookupFileReq* /* req */,
                  LookupFileResp* /* resp */) override {
    return false;
  }

  // Dump recorded StoreFileReqs into filename.
  bool Dump(const string& filename) const;

 private:
  std::unique_ptr<StoreFileReq> req_;

  DISALLOW_COPY_AND_ASSIGN(FileServiceDumpClient);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_GOMA_FILE_DUMP_H_
