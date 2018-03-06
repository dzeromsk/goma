// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CPP_INPUT_H_
#define DEVTOOLS_GOMA_CLIENT_CPP_INPUT_H_

#include <memory>
#include <string>

#include "content.h"
#include "cpp_input_stream.h"
#include "include_guard_detector.h"

namespace devtools_goma {

class CppInput {
 public:
  CppInput(std::unique_ptr<Content> content, const FileId& fileid,
           const string& filepath, const string& directory,
           int include_dir_index)
      : filepath_(filepath),
        directory_(directory), include_dir_index_(include_dir_index),
        stream_(std::move(content), fileid, filepath) {
  }

  const string& filepath() const { return filepath_; }
  const string& directory() const { return directory_; }
  const FileId& fileid() const { return stream_.fileid(); }
  int include_dir_index() const { return include_dir_index_; }

  CppInputStream* stream() { return &stream_; }

  IncludeGuardDetector* include_guard_detector() {
    return &include_guard_detector_;
  }

 private:
  const string filepath_;
  const string directory_;
  const int include_dir_index_;

  CppInputStream stream_;
  IncludeGuardDetector include_guard_detector_;

  DISALLOW_COPY_AND_ASSIGN(CppInput);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CPP_INPUT_H_
