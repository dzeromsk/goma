// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_LIST_DIR_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_LIST_DIR_CACHE_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "atomic_stats_counter.h"
#include "file_dir.h"
#include "file_stat.h"
#include "linked_unordered_map.h"
#include "lockhelper.h"

namespace devtools_goma {

class ListDirCache {
 public:
  static ListDirCache* instance() {
    return instance_;
  }

  static void Init(size_t max_entries);
  static void Quit();

  /*
   * This function returns directory entries in |path| via |entries|.
   * |filestat| is used to check cached result is stale or not.
   * This function returns false iff |path| is not directory.
   * This function is thread-safe.
   */
  bool GetDirEntries(const std::string& path,
                     const FileStat& filestat,
                     std::vector<DirEntry>* entries);

  int64_t hit() const {
    return hit_.value();
  }

  int64_t miss() const {
    return miss_.value();
  }

 private:
  explicit ListDirCache(size_t max_entries)
      : max_entries_(max_entries), current_entries_(0) {}
  ListDirCache(const ListDirCache&) = delete;
  ListDirCache& operator=(const ListDirCache&) = delete;

  static ListDirCache* instance_;

  const size_t max_entries_;

  StatsCounter hit_;
  StatsCounter miss_;

  ReadWriteLock rwlock_;
  size_t current_entries_ GUARDED_BY(rwlock_);
  LinkedUnorderedMap<std::string, std::pair<FileStat, std::vector<DirEntry>>>
      dir_entries_cache_ GUARDED_BY(rwlock_);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_LIST_DIR_CACHE_H_
