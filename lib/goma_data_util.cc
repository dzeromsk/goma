// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "lib/goma_data_util.h"

#include <algorithm>
#include <string>
#include <vector>

#include "goma_hash.h"

#include "prototmp/goma_data.pb.h"
using std::string;

namespace devtools_goma {

bool IsSameSubprograms(const ExecReq& req, const ExecResp& resp) {
  if (req.subprogram_size() != resp.result().subprogram_size()) {
    return false;
  }

  std::vector<string> req_hashes;
  for (const auto& subprogram : req.subprogram()) {
    req_hashes.push_back(subprogram.binary_hash());
  }
  std::vector<string> resp_hashes;
  for (const auto& subprogram : resp.result().subprogram()) {
    resp_hashes.push_back(subprogram.binary_hash());
  }
  std::sort(req_hashes.begin(), req_hashes.end());
  std::sort(resp_hashes.begin(), resp_hashes.end());
  return req_hashes == resp_hashes;
}

bool IsValidFileBlob(const FileBlob& blob) {
  if (!blob.has_file_size())
    return false;
  if (blob.file_size() < 0)
    return false;

  switch (blob.blob_type()) {
    case FileBlob::FILE_UNSPECIFIED:
      return false;

    case FileBlob::FILE:
      if (blob.has_offset())
        return false;
      if (!blob.has_content())
        return false;
      if (blob.hash_key_size() > 0)
        return false;
      return true;

    case FileBlob::FILE_META:
      if (blob.has_offset())
        return false;
      if (blob.has_content())
        return false;
      if (blob.hash_key_size() <= 1)
        return false;
      return true;

    case FileBlob::FILE_CHUNK:
      if (!blob.has_offset())
        return false;
      if (!blob.has_content())
        return false;
      if (blob.hash_key_size() > 0)
        return false;
      return true;
  }
}

string ComputeFileBlobHashKey(const FileBlob& blob) {
  string s;
  blob.SerializeToString(&s);
  string md_str;
  ComputeDataHashKey(s, &md_str);
  return md_str;
}

}  // namespace devtools_goma
