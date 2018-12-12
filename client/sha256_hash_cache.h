// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_SHA256_HASH_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_SHA256_HASH_CACHE_H_

#include <string>
#include <unordered_map>

#include "atomic_stats_counter.h"
#include "file_stat.h"
#include "lockhelper.h"

namespace devtools_goma {

class SHA256HashCache {
 public:

  // If |path| exsts in |sha256_cache| and filestat is not updated,
  // the value is returned.
  // Otherwise, calculate sha256 hash from |path|, and put the result
  // to |sha256_cache| with filestat.
  // Returns false if calculating sha256 hash from |path| failed.
  bool GetHashFromCacheOrFile(const string& path, string* hash);

  int64_t total() const { return total_.value(); }
  int64_t hit() const { return hit_.value(); }

 private:
  friend class SHA256HashCacheTest;

  using ValueT = std::pair<FileStat, std::string>;
  ReadWriteLock mu_;
  // |filepath| -> (filestat, hash of file)
  std::unordered_map<std::string, ValueT> cache_ GUARDED_BY(mu_);

  // counter for test.
  StatsCounter total_;
  StatsCounter hit_;
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_SHA256_HASH_CACHE_H_
