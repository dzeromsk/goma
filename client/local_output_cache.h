// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_LOCAL_OUTPUT_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_LOCAL_OUTPUT_CACHE_H_

#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "atomic_stats_counter.h"
#include "autolock_timer.h"
#include "compiler_specific.h"
#include "goma_hash.h"
#include "linked_unordered_map.h"
#include "sha256hash_hasher.h"
#include "worker_thread_manager.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_data.pb.h"
#include "prototmp/local_output_cache_data.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

class LocalOutputCacheStats;

// LocalOutputCache is a cache that ExecReq -> output files.
class LocalOutputCache {
 public:
  struct GarbageCollectionStat {
    size_t num_removed = 0;          // # of garbage collected entries
    size_t num_failed = 0;           // failed to remove
    std::int64_t removed_bytes = 0;  // total removed bytes
  };

  static bool IsEnabled() { return instance_ != nullptr; }
  static LocalOutputCache* instance() { return instance_; }

  // When |server| is nullptr, GC won't run. This will be useful
  // for testing.
  static void Init(std::string cache_dir,
                   WorkerThreadManager* wm,
                   int max_cache_amount_in_mb,
                   int threshold_cache_amount_in_mb,
                   size_t max_cache_items,
                   size_t threshold_cache_items);
  static void Quit();

  LocalOutputCache(const LocalOutputCache&) = delete;
  LocalOutputCache(LocalOutputCache&&) = delete;
  LocalOutputCache& operator=(const LocalOutputCache&) = delete;
  LocalOutputCache& operator=(LocalOutputCache&&) = delete;

  // Creates cache key from |req|.
  static std::string MakeCacheKey(const ExecReq& req);

  // SaveOutput copies output files to cache.
  // |trace_id| is just used for logging.
  bool SaveOutput(const std::string& key,
                  const ExecReq* req,
                  const ExecResp* resp,
                  const std::string& trace_id);

  // Finds cache with |key|.
  // Returns true when a cache is found and read correctly. In this case,
  // |resp| will be filled with output data.
  // Otherwise, false is returned.
  // |trace_id| is just used for logging.
  bool Lookup(const std::string& key,
              ExecResp* resp,
              const std::string& trace_id);

  // Dumps stats.
  void DumpStatsToProto(LocalOutputCacheStats* stats);

  // For stats. These will be removed after merged to GomaStats.
  size_t TotalCacheCount();
  std::int64_t TotalCacheAmountInByte();
  size_t TotalGCRemovedItems() const { return stats_gc_removed_items_.value(); }
  std::int64_t TotalGCRemovedBytes() const {
    return stats_gc_removed_bytes_.value();
  }

 private:
  struct CacheEntry {
    CacheEntry() : mtime(0), amount_byte(0) {}
    CacheEntry(time_t mtime, std::int64_t amount_byte)
        : mtime(mtime), amount_byte(amount_byte) {
    }
    ~CacheEntry() {
    }

    time_t mtime;
    std::int64_t amount_byte;
  };

  LocalOutputCache(std::string cache_dir,
                   std::int64_t max_cache_amount_byte,
                   std::int64_t threashold_cache_amount_byte,
                   size_t max_cache_items,
                   size_t threshold_cache_items);
  ~LocalOutputCache();

  // load cache entries.
  void StartLoadCacheEntries(WorkerThreadManager* wm);
  void LoadCacheEntries();
  void LoadCacheEntriesDone();
  // Wait until all cache entries are loaded from the file.
  void WaitUntilReady();

  void AddCacheEntry(const SHA256HashValue& key,
                     std::int64_t cache_amount_in_byte);
  // A cache entry is updated, so move it to last.
  void UpdateCacheEntry(const SHA256HashValue& key);

  void StartGarbageCollection(WorkerThreadManager* wm)
      LOCKS_EXCLUDED(entries_mu_);
  void StopGarbageCollection() LOCKS_EXCLUDED(entries_mu_);
  void GarbageCollectionThread() LOCKS_EXCLUDED(entries_mu_);
  bool ShouldInvokeGarbageCollection() const
      LOCKS_EXCLUDED(entries_mu_);
  bool ShouldInvokeGarbageCollectionUnlocked() const
      SHARED_LOCKS_REQUIRED(entries_mu_);
  bool ShouldContinueGarbageCollectionUnlocked() const
      SHARED_LOCKS_REQUIRED(entries_mu_);
  void RunGarbageCollection(GarbageCollectionStat* stat)
      LOCKS_EXCLUDED(entries_mu_);
  void WakeGCThread() LOCKS_EXCLUDED(entries_mu_);
  void WaitUntilGarbageCollectionThreadDone() LOCKS_EXCLUDED(entries_mu_);

  // Used only for test.
  void SetReady(bool ready);

  // Full path of cache directory + key prefix.
  std::string CacheDirWithKeyPrefix(absl::string_view key) const;
  // Full path of cache directory + key prefix + key.
  std::string CacheFilePath(absl::string_view key) const;

  static LocalOutputCache* instance_;

  // LocalOutputCache configurations
  const std::string cache_dir_;
  const std::int64_t max_cache_amount_byte_;
  const std::int64_t threshold_cache_amount_byte_;
  const size_t max_cache_items_;
  const size_t threshold_cache_items_;

  // Using in initial load of cache entries.
  // After loading all cache entries, |ready_| will become true.
  mutable Lock ready_mu_;
  ConditionVariable ready_cond_;
  bool ready_ GUARDED_BY(ready_mu_);

  // cache entries. Older cache is first.
  using CacheEntryMap =
      LinkedUnorderedMap<SHA256HashValue, CacheEntry, SHA256HashValueHasher>;
  mutable ReadWriteLock entries_mu_ ACQUIRED_AFTER(gc_mu_);
  CacheEntryMap entries_ GUARDED_BY(entries_mu_);
  // total cache amount in bytes.
  std::int64_t entries_total_cache_amount_ GUARDED_BY(entries_mu_);

  mutable Lock gc_mu_;
  ConditionVariable gc_cond_;
  bool gc_should_done_ GUARDED_BY(gc_mu_);
  bool gc_working_ GUARDED_BY(gc_mu_);

  StatsCounter stats_save_success_;
  StatsCounter stats_save_success_time_ms_;
  StatsCounter stats_save_failure_;

  StatsCounter stats_lookup_success_;
  StatsCounter stats_lookup_success_time_ms_;
  StatsCounter stats_lookup_miss_;
  StatsCounter stats_lookup_failure_;

  StatsCounter stats_commit_success_;
  StatsCounter stats_commit_success_time_ms_;
  StatsCounter stats_commit_failure_;

  StatsCounter stats_gc_count_;
  StatsCounter stats_gc_total_time_ms_;

  StatsCounter stats_gc_removed_items_;
  StatsCounter stats_gc_removed_bytes_;
  StatsCounter stats_gc_failed_items_;

  friend class LocalOutputCacheTest;
};

} // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_LOCAL_OUTPUT_CACHE_H_
