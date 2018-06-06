// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_INCLUDE_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_INCLUDE_CACHE_H_

#include <list>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/types/optional.h"
#include "atomic_stats_counter.h"
#include "autolock_timer.h"
#include "cpp_directive.h"
#include "goma_hash.h"
#include "include_item.h"
#include "linked_unordered_map.h"

using std::string;

namespace devtools_goma {

struct FileStat;
class IncludeCacheStats;

// IncludeCache stores include files which contains only directives.
class IncludeCache {
 public:
  static IncludeCache* instance() { return instance_; }
  static bool IsEnabled() { return instance_ != NULL; }

  // Initializes IncludeCache.
  // |max_cache_size_in_mb| specifies the maximum amount of cache size. If cache
  // size exceeds this value, the oldest cache will be evicted.
  // When |calculates_directive_hash| is true, we also calculate the hash value
  // of cache item. This value will be used from DepsCache.
  static void Init(int max_cache_size_in_mb, bool calculates_directive_hash);
  static void Quit();

  // Get IncludeItem from cache or file.
  // If it does not exist in the cache, reat it from file and parse it.
  IncludeItem GetIncludeItem(const string& filepath, const FileStat& file_stat);

  // Get directive hash. If we have a cache and its FileStat is the same as
  // |file_stat|, we return the cached one. Otherwise, we calculate the
  // directive hash, and save it. If |filepath| is not found, invalid hash value
  // is returned.
  absl::optional<SHA256HashValue> GetDirectiveHash(const string& filepath,
                                                   const FileStat& file_stat);

  void Dump(std::ostringstream* ss);
  static void DumpAll(std::ostringstream* ss);

  void DumpStatsToProto(IncludeCacheStats* stats);

  bool calculates_directive_hash() const { return calculates_directive_hash_; }

 private:
  class Item;
  friend class IncludeCacheTest;

  IncludeCache(size_t max_cache_size, bool calculates_directive_hash);
  ~IncludeCache();

  const IncludeCache::Item* GetItemIfNotModifiedUnlocked(
      const string& key,
      const FileStat& file_stat) const SHARED_LOCKS_REQUIRED(rwlock_);
  void InsertUnlocked(const string& key,
                      std::unique_ptr<Item> include_item,
                      const FileStat& file_stat)
      EXCLUSIVE_LOCKS_REQUIRED(rwlock_);
  void EvictCacheUnlocked() EXCLUSIVE_LOCKS_REQUIRED(rwlock_);

  static IncludeCache* instance_;

  const bool calculates_directive_hash_;

  ReadWriteLock rwlock_;
  // A map from filepath to unique_ptr<Item>.
  // The oldest item comes first.
  // TODO: We might want to use LRU instead of just queue.
  // Currently we're not updating |cache_items_| after referring.
  LinkedUnorderedMap<std::string, std::unique_ptr<Item>> cache_items_;

  size_t count_item_updated_;
  size_t count_item_evicted_;
  // The total content size of cached items.
  size_t current_cache_size_;
  size_t max_cache_size_;

  StatsCounter hit_count_;
  StatsCounter missed_count_;

  DISALLOW_COPY_AND_ASSIGN(IncludeCache);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_INCLUDE_CACHE_H_
