// Copyright 2010 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Hold file cache state for compiler_proxy
//

#include <fcntl.h>
#include <sys/types.h>

#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "absl/time/clock.h"
#include "atomic_stats_counter.h"
#include "autolock_timer.h"
#include "env_flags.h"
#include "file_hash_cache.h"
#include "glog/logging.h"
#include "path.h"
#include "util.h"

using std::string;

namespace devtools_goma {

// Returns cache ID if it was found in cache.
bool FileHashCache::GetFileCacheKey(const string& filename,
                                    absl::optional<absl::Time> missed_timestamp,
                                    const FileStat& file_stat,
                                    string* cache_key) {
  DCHECK(file::IsAbsolutePath(filename)) << filename;
  cache_key->clear();

  if (!file_stat.IsValid()) {
    LOG(INFO) << "Clear cache: file_stat is invalid: " << filename;
    AUTO_EXCLUSIVE_LOCK(lock, &file_cache_mutex_);
    file_cache_.erase(filename);
    num_stat_error_.Add(1);
    return false;
  }

  FileInfo info;
  {
    AUTO_SHARED_LOCK(lock, &file_cache_mutex_);
    std::unordered_map<string, struct FileInfo>::iterator it =
        file_cache_.find(filename);
    if (it == file_cache_.end()) {
      num_cache_miss_.Add(1);
      return false;
    }
    info = it->second;
    num_cache_hit_.Add(1);
  }

  // found in cache.  Verify (reasonably) that it is the one that are looking
  // for, using lightweight information.
  if (file_stat == info.file_stat) {
    *cache_key = info.cache_key;
    bool valid = true;
    if (missed_timestamp.has_value()) {
      valid = missed_timestamp <= info.last_uploaded_timestamp;
      VLOG_IF(2, valid) << "uploaded after missing input request? "
                        << filename
                        << " missed=" << *missed_timestamp
                        << " uploaded="
                        << OptionalToString(info.last_uploaded_timestamp);
    }
    // As of this comment, |info.file_stat.mtime| is guaranteed to have a valid
    // value because of the call to file_stat.IsValid() earlier. However, if
    // that changes in the future, we would like to catch it and add a
    // has_value() check to here as well.
    DCHECK(info.file_stat.mtime.has_value());
    if (valid && info.last_checked.has_value() &&
        *info.last_checked > *info.file_stat.mtime) {
      // We are reasonably confident that we found the right information.
      return true;
    }
    VLOG(1) << "might be obsolete cache: " << filename << " " << *cache_key;
    return false;
  }

  AUTO_EXCLUSIVE_LOCK(lock, &file_cache_mutex_);
  LOG(INFO) << "Clear obsolete cache: " << filename << " " << *cache_key;
  file_cache_.erase(filename);
  num_clear_obsolete_.Add(1);
  return false;
}

// TODO: there is a race condition that if file changed
// between send and receive, it won't be detected correctly. Fix
// that later if it's a problem..
bool FileHashCache::StoreFileCacheKey(const string& filename,
                                      const string& cache_key,
                                      absl::optional<absl::Time>
                                          upload_timestamp,
                                      const FileStat& file_stat) {
  if (!file_stat.IsValid()) {
    LOG(WARNING) << "Try to store, but clear cache: failed taking FileStat: "
                 << filename;
    // Remove the cache key if it's not found in the cache.
    AUTO_EXCLUSIVE_LOCK(lock, &file_cache_mutex_);
    file_cache_.erase(filename);
    num_clear_cache_.Add(1);
    // we don't clear cache key from known_cache_keys_, because other file
    // may have the same cache_key (copied content).
    return false;
  }

  {
    FileInfo info;
    info.cache_key = cache_key;
    info.file_stat = file_stat;
    info.last_checked = absl::Now();
    info.last_uploaded_timestamp = upload_timestamp;

    AUTO_EXCLUSIVE_LOCK(lock, &file_cache_mutex_);

    std::pair<std::unordered_map<string, struct FileInfo>::iterator, bool> p =
        file_cache_.insert(make_pair(filename, info));
    if (!p.second) {
      if (!info.last_uploaded_timestamp.has_value()) {
        info.last_uploaded_timestamp = p.first->second.last_uploaded_timestamp;
      }
      p.first->second = info;
    }
    num_store_cache_.Add(1);
  }

  AUTO_EXCLUSIVE_LOCK(lock, &known_cache_keys_mutex_);
  std::pair<std::unordered_set<string>::iterator, bool> p2 =
      known_cache_keys_.insert(cache_key);
  return p2.second;
}

bool FileHashCache::IsKnownCacheKey(const string& cache_key) {
  AUTO_SHARED_LOCK(lock, &known_cache_keys_mutex_);
  return known_cache_keys_.count(cache_key) > 0;
}

FileHashCache::FileHashCache() {
}

string FileHashCache::DebugString() {
  std::stringstream ss;
  ss << "[GetFileCacheKey]" << std::endl;
  ss << "cache hit=" << num_cache_hit_.value() << std::endl;
  ss << "cache miss=" << num_cache_miss_.value() << std::endl;
  ss << "stat error=" << num_stat_error_.value() << std::endl;
  ss << "clear obsolete=" << num_clear_obsolete_.value() << std::endl;
  ss << "[StoreFileCacheKey]" << std::endl;
  ss << "store cache=" << num_store_cache_.value() << std::endl;
  ss << "clear cache=" << num_clear_cache_.value() << std::endl << std::endl;

  AUTO_SHARED_LOCK(lock, &file_cache_mutex_);
  ss << "[file_cache] size=" << file_cache_.size() << std::endl;
  for (const auto& it : file_cache_) {
    ss << "filename:" << it.first << " key:" << it.second.cache_key
       << " file_size:" << it.second.file_stat.size
       << " mtime:";
    if (it.second.file_stat.mtime.has_value()) {
      ss << *it.second.file_stat.mtime;
    } else {
      ss << "(unknown)";
    }
    ss << std::endl;
  }
  return ss.str();
}

}  // namespace devtools_goma
