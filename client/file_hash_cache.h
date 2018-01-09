// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILE_HASH_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_FILE_HASH_CACHE_H_

#include <string>

#include "atomic_stats_counter.h"
#include "basictypes.h"
#include "file_id.h"
#include "lockhelper.h"
#include "thread_annotations.h"
#include "timestamp.h"
#include "unordered.h"

using std::string;

namespace devtools_goma {

class FileHashCache {
 public:
  FileHashCache();

  // Gets hash code (cache key) of |filename|.
  // Returns true if it has cache key.
  // Returns false and *cache_key is not empty, if *cache_key was used for
  // cache key of the file but is not sure in some race condition because
  // mtime granularity is second.
  //   X.xx sec: last checked, hash_key is H1.
  //   X.yy sec: file is modified.
  //   X.zz sec: check the cache. mtime is X, the same as X.xx sec,
  //             but hash key might be H1 (not modified at X.yy)
  //             or might not be H1 (modified at X.yy)
  // If |filename| exists and |missed_timestamp_ms| is not 0, cache_key will
  // be valid if |missed_timestamp_ms| <= |last_uploaded_timestamp_ms|.
  // cache_key will be invalidated if |missed_timestamp_ms| >
  // |last_uploaded_timestamp_ms|.
  // FileId for |filename| is |file_id|.
  // We don't take |file_id_cache| ownership.
  // If |missed_timestamp_ms| is 0, this check won't be performed.
  // Returns false and *cache_key is empty if it doesn't know cache key of the
  // file at all.
  bool GetFileCacheKey(const string& filename,
                       millitime_t missed_timestamp_ms,
                       const FileId& file_id,
                       string* cache_key);

  // Stores hash code (cache key) of |filename|.
  // |upload_timestamp_ms| is upload time or download time of the file
  // in milliseconds.
  // Please set 0LL if you do not upload or download the file. It preserves
  // last_uploaded_timestamp_ms.
  // |file_id| is a FileId of |filename|.
  // If |file_id| is invalid, it clears the cache_key of the filename,
  // and returns false.
  // Returns true if the cache_key is the first used in FileCacheKey.
  // Returns false if the cache_key was used before or |file_id| is invalid.
  bool StoreFileCacheKey(const string &filename, const string& cache_key,
                         millitime_t upload_timestamp_ms,
                         const FileId& file_id);

  bool IsKnownCacheKey(const string& cache_key);

  string DebugString();

 private:
  struct FileInfo {
    string cache_key;
    FileId file_id;
    // time when hash key was stored in cache.
    // FileInfo represents valid hash key of local file if mtime < last_checked.
    time_t last_checked;

    // time when file content was uploaded to backend, or downloaded from
    // backend.
    // we could assume the file has been in remote cache and use hash_key
    // at time t if last_uploaded_timestamp_ms != 0 &&
    // t > last_uploaded_timestamp_ms.
    millitime_t last_uploaded_timestamp_ms;
  };

  // A map from filename to file cache info.
  ReadWriteLock file_cache_mutex_;
  unordered_map<string, struct FileInfo> file_cache_
    GUARDED_BY(file_cache_mutex_);

  // A set of cache keys that have been stored, so we could believe a cache_key
  // in this set is in goma cache.
  ReadWriteLock known_cache_keys_mutex_;
  unordered_set<string> known_cache_keys_ GUARDED_BY(known_cache_keys_mutex_);

  StatsCounter num_cache_hit_;
  StatsCounter num_cache_miss_;
  StatsCounter num_stat_error_;
  StatsCounter num_clear_obsolete_;
  StatsCounter num_store_cache_;
  StatsCounter num_clear_cache_;

  DISALLOW_COPY_AND_ASSIGN(FileHashCache);
};

}  // namespace devtools_goma
#endif  // DEVTOOLS_GOMA_CLIENT_FILE_HASH_CACHE_H_
