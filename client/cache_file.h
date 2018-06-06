// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_CACHE_FILE_H_
#define DEVTOOLS_GOMA_CLIENT_CACHE_FILE_H_

#include <string>

#include "basictypes.h"

namespace google {
namespace protobuf {
class Message;
}  // namespace protobuf
}  // namespace google

namespace devtools_goma {

// CacheFile manages cache file of serialized protocol buffer message.
// It also saves sha256 sum of cache file in *.sha256 file to detect file
// corruption. it checks sha256 matches with cache file when loading.
class CacheFile {
 public:
  explicit CacheFile(std::string filename);
  ~CacheFile();

  bool Load(google::protobuf::Message* data) const;
  // Load message with max limit. if |total_bytes_limit| < 0
  // and warning_threshold < 0, the default limit will be used.
  bool LoadWithMaxLimit(google::protobuf::Message* data,
                        int total_bytes_limit,
                        int warning_threshold) const;
  bool Save(const google::protobuf::Message& data) const;

  const std::string& filename() const { return filename_; }
  bool Enabled() const { return !filename_.empty(); }

 private:
  std::string filename_;

  DISALLOW_COPY_AND_ASSIGN(CacheFile);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CACHE_FILE_H_
