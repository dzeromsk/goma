// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sha256_hash_cache.h"

#include "autolock_timer.h"
#include "goma_hash.h"

namespace devtools_goma {

bool SHA256HashCache::GetHashFromCacheOrFile(const string& path, string* hash) {
  total_.Add(1);

  FileStat filestat(path);
  if (!filestat.IsValid()) {
    return false;
  }

  {
    AUTO_SHARED_LOCK(lock, &mu_);
    const auto& it = cache_.find(path);
    if (it != cache_.end() &&
        !filestat.CanBeNewerThan(it->second.first, now_fn_())) {
      *hash = it->second.second;
      hit_.Add(1);
      return true;
    }
  }

  if (!GomaSha256FromFile(path, hash)) {
    return false;
  }

  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  cache_[path] = std::make_pair(filestat, *hash);
  return true;
}

}  // namespace devtools_goma
