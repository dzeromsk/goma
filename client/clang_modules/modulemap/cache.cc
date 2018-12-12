// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cache.h"

#include "autolock_timer.h"
#include "base/path.h"
#include "glog/logging.h"

namespace devtools_goma {
namespace modulemap {

// static
Cache* Cache::instance_;

// static
void Cache::Init(size_t cache_size) {
  CHECK(instance_ == nullptr)
      << "modulemap::Cache has already been initialized?";
  instance_ = new Cache(cache_size);
}

// static
void Cache::Quit() {
  CHECK(instance_ != nullptr) << "modulemap::Cache was not initialized?";
  delete instance_;
  instance_ = nullptr;
}

size_t Cache::size() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return cache_.size();
}

bool Cache::AddModuleMapFileAndDependents(const string& module_map_file,
                                          const string& cwd,
                                          std::set<string>* include_files,
                                          FileStatCache* file_stat_cache) {
  // first, find from cache. If found, check all FileStat.
  // If nothing is changed, we just use it.
  string abs_module_map_path =
      file::JoinPathRespectAbsolute(cwd, module_map_file);
  CacheKey key(cwd, std::move(abs_module_map_path));

  bool cache_hit = false;
  std::vector<CollectedModuleMapFile> cached_item;
  {
    AUTO_SHARED_LOCK(lock, &mu_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      // Need to copy in order to access outside of lock.s
      cache_hit = true;
      cached_item = it->second;
    }
  }

  if (cache_hit) {
    bool ok = true;
    // If cache_hit, check FileStat.
    for (const auto& cf : cached_item) {
      FileStat fs = file_stat_cache->Get(cf.abs_path);
      if (!fs.IsValid() || fs.CanBeNewerThan(cf.file_stat)) {
        // a file is deleted or changed.
        ok = false;
        break;
      }
    }

    if (ok) {
      // All dependent files aren't changed.
      for (const auto& cf : cached_item) {
        include_files->insert(cf.rel_path);
      }
      cache_hit_.Add(1);
      return true;
    }
  }

  cache_miss_.Add(1);

  // If cache is not found or invalidated, we just run the processor, and keep
  // the result.
  Processor processor(cwd, file_stat_cache);
  if (!processor.AddModuleMapFile(module_map_file)) {
    return false;
  }

  for (const auto& cf : processor.collected_module_map_files()) {
    include_files->insert(cf.rel_path);
  }

  for (const auto& cf : processor.collected_module_map_files()) {
    if (cf.file_stat.CanBeStale()) {
      // Do not cache if stat can be stale.
      return true;
    }
  }

  {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    cache_.emplace_back(
        std::move(key),
        std::move(*processor.mutable_collected_module_map_files()));

    // Remove oldest entries.
    while (cache_.size() > max_cache_entries_) {
      cache_.pop_front();
      cache_evicted_.Add(1);
    }
  }

  return true;
}

}  // namespace modulemap
}  // namespace devtools_goma
