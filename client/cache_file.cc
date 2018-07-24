// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "cache_file.h"

#include <fstream>
#include <utility>

#include "file_helper.h"
#include "glog/logging.h"
#include "goma_hash.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/message.h"

namespace devtools_goma {

using std::string;

CacheFile::CacheFile(string filename) : filename_(std::move(filename)) {}

CacheFile::~CacheFile() {}

bool CacheFile::Load(google::protobuf::Message* msg) const {
  return LoadWithMaxLimit(msg, -1, -1);
}

bool CacheFile::LoadWithMaxLimit(google::protobuf::Message* msg,
                                 int total_bytes_limit,
                                 int warning_threshold) const {
  const string sha256_path = filename_ + ".sha256";
  {
    // First, check *.sha256, so that it is not corrupted.
    string sha256_expected;
    if (!ReadFileToString(sha256_path, &sha256_expected)) {
      LOG(INFO) << sha256_path << " does not exist.";
      return false;
    }
    string sha256_actual;
    if (!GomaSha256FromFile(filename_, &sha256_actual)) {
      LOG(INFO) << "failed to calculate sha256 of " << filename_;
      return false;
    }

    if (sha256_actual != sha256_expected) {
      LOG(ERROR) << "sha256 digest of " << filename_ << ": " << sha256_actual
                 << " but expected: " << sha256_expected;
      return false;
    }

    LOG(INFO) << filename_ << " integrity OK.";
  }

  std::ifstream f(filename_.c_str(), std::ifstream::binary);
  if (!f.is_open()) {
    LOG(INFO) << "failed to open " << filename_;
    return false;
  }

  // Note: FileInputStream is more efficient than IstreamInputStream.
  // However, FileInputStream takes fd and we need to support Windows.
  google::protobuf::io::IstreamInputStream iis(&f);
  google::protobuf::io::CodedInputStream input(&iis);
  if (total_bytes_limit >= 0 && warning_threshold >= 0) {
    input.SetTotalBytesLimit(total_bytes_limit, warning_threshold);
  } else if (total_bytes_limit >= 0 || warning_threshold >= 0) {
    LOG(ERROR) << "only one of total_bytes_limit or warning_threshold"
               << " is set. Set both."
               << " total_bytes_limit=" << total_bytes_limit
               << " warning_threshold=" << warning_threshold;
  }

  if (!msg->ParseFromCodedStream(&input)) {
    LOG(ERROR) << "failed to parse " << filename_;
    return false;
  }

  return true;
}

bool CacheFile::Save(const google::protobuf::Message& msg) const {
  {
    string msg_buf;
    msg.SerializeToString(&msg_buf);
    if (!WriteStringToFile(msg_buf, filename_)) {
      LOG(ERROR) << "failed to write " << filename_;
      return false;
    }
  }

  // Calculate sha256 of filename_, so that we can check it's not corrupted.
  const string sha256_path = filename_ + ".sha256";
  string sha256_str;
  if (!GomaSha256FromFile(filename_, &sha256_str)) {
    LOG(ERROR) << "failed to calculate sha256 of " << filename_;
    if (remove(filename_.c_str()) != 0) {
      LOG(ERROR) << "failed to delete corrupted " << filename_;
    }
    return false;
  }

  if (!WriteStringToFile(sha256_str, sha256_path)) {
    LOG(ERROR) << "failed to write " << sha256_path;
    return false;
  }
  return true;
}

}  // namespace devtools_goma
