// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_CACHE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "client/atomic_stats_counter.h"
#include "client/file_stat.h"
#include "client/linked_unordered_map.h"
#include "lockhelper.h"
#include "processor.h"

using std::string;

namespace devtools_goma {
namespace modulemap {

// Cache is a module map cache.
// Thread-safe.
class Cache {
 public:
  static void Init(size_t max_cache_entries);
  static void Quit();
  static Cache* instance() { return instance_; }

  // Lists dependent module map files from |module_map_file|, and
  // inserts dependent files (and |module_map_file|, too) into
  // |include_files|.
  // If cache is found, cache is used instead of parsing |module_map_file|.
  // Returns true if succeeded.
  // false otherwise (e.g. a file is missing, a file is not valid
  // module-map-file).
  bool AddModuleMapFileAndDependents(const string& module_map_file,
                                     const string& cwd,
                                     std::set<string>* include_files,
                                     FileStatCache* file_stat_cache);

  size_t size() const;

  // Stat. Returns cache hit count.
  std::int64_t cache_hit() const { return cache_hit_.value(); }
  // Stat. Returns cache miss count.
  std::int64_t cache_miss() const { return cache_miss_.value(); }
  // Stat. Returns cache evicted count.
  std::int64_t cache_evicted() const { return cache_evicted_.value(); }

 private:
  // Cache Key. Since a relative path is collected, we have to keep
  // cwd besides abs_module_map_file.
  struct CacheKey {
    CacheKey() = default;
    CacheKey(string cwd, string abs_module_map_file)
        : cwd(std::move(cwd)),
          abs_module_map_file(std::move(abs_module_map_file)) {}

    template <typename H>
    friend H AbslHashValue(H h, const CacheKey& c) {
      return H::combine(std::move(h), c.cwd, c.abs_module_map_file);
    }

    friend bool operator==(const CacheKey& lhs, const CacheKey& rhs) {
      return lhs.cwd == rhs.cwd &&
             lhs.abs_module_map_file == rhs.abs_module_map_file;
    }
    friend bool operator!=(const CacheKey& lhs, const CacheKey& rhs) {
      return !(lhs == rhs);
    }

    string cwd;
    string abs_module_map_file;
  };

  explicit Cache(size_t max_cache_entries)
      : max_cache_entries_(max_cache_entries) {}

  Cache(const Cache&) = delete;
  void operator=(const Cache&) = delete;

  static Cache* instance_;

  const size_t max_cache_entries_;

  mutable ReadWriteLock mu_;
  LinkedUnorderedMap<CacheKey, std::vector<CollectedModuleMapFile>> cache_
      GUARDED_BY(mu_);

  StatsCounter cache_hit_;
  StatsCounter cache_miss_;
  StatsCounter cache_evicted_;

  friend class ModuleMapCacheTest;
};

}  // namespace modulemap
}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_CLANG_MODULES_MODULEMAP_CACHE_H_
