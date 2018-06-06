// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_LIB_GOMA_HASH_H_
#define DEVTOOLS_GOMA_LIB_GOMA_HASH_H_

#include <ostream>
#include <string>


#include "absl/strings/string_view.h"
using std::string;

namespace devtools_goma {

class SHA256HashValue {
 public:
  SHA256HashValue() : data_{} {}

  static bool ConvertFromHexString(const string& hex_string,
                                   SHA256HashValue* hash_value);

  string ToHexString() const;

  unsigned char* mutable_data() { return data_; }
  const unsigned char* data() const { return data_; }

  // Make hash for unordered_map.
  size_t Hash() const;

  friend bool operator==(const SHA256HashValue& lhs,
                         const SHA256HashValue& rhs) {
    return memcmp(lhs.data_, rhs.data_, sizeof(lhs.data_)) == 0;
  }

  friend bool operator!=(const SHA256HashValue& lhs,
                         const SHA256HashValue& rhs) {
    return !(lhs == rhs);
  }

  friend bool operator<(const SHA256HashValue& lhs,
                        const SHA256HashValue& rhs) {
    return memcmp(lhs.data_, rhs.data_, sizeof(lhs.data_)) < 0;
  }

  friend std::ostream& operator<<(std::ostream& os, const SHA256HashValue& v) {
    return os << v.ToHexString();
  }

 private:
  unsigned char data_[32];
};

void ComputeDataHashKeyForSHA256HashValue(absl::string_view data,
                                          SHA256HashValue* hash_value);

void ComputeDataHashKey(absl::string_view data, string* md_str);
bool GomaSha256FromFile(const string& filename, string* md_str);

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_LIB_GOMA_HASH_H_
