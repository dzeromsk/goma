// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef DEVTOOLS_GOMA_BASE_STATUS_H_
#define DEVTOOLS_GOMA_BASE_STATUS_H_

namespace util {

// TODO: consider to use google/protobuf/stubs/status if necessary.
class Status {
 public:
  explicit Status(bool ok) : ok_(ok) {}

  bool ok() const { return ok_; }

 private:
  const bool ok_;
};

}  // namespace util

#endif  // DEVTOOLS_GOMA_BASE_STATUS_H_
