// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "list_dir_cache.h"

#include "absl/time/clock.h"
#include "autolock_timer.h"
#include "counterz.h"

namespace devtools_goma {

namespace {

// Rounds |time| down to a whole number of seconds since the Unix Epoch.
absl::Time TruncateTimeToSeconds(absl::Time time) {
  const absl::Duration duration_from_epoch = time - absl::UnixEpoch();
  return absl::UnixEpoch() + absl::Trunc(duration_from_epoch, absl::Seconds(1));
}

}  // namespace

ListDirCache* ListDirCache::instance_;

/* static */
void ListDirCache::Init(size_t max_entries) {
  instance_ = new ListDirCache(max_entries);
}

/* static */
void ListDirCache::Quit() {
  delete instance_;
  instance_ = nullptr;
}

bool ListDirCache::GetDirEntries(const std::string& path,
                                 const FileStat& filestat,
                                 std::vector<DirEntry>* entries) {
  entries->clear();
  GOMA_COUNTERZ("total");

  // TODO: There is some weird behavior when converting FileStat from
  // time_t (integers with seconds resolution) to absl::Time (could have
  // subsecond resolution, depending on the platform). For now, simply replicate
  // the previous behavior by rounding down to whole seconds.
  FileStat filestat_rounded = filestat;
  if (filestat.mtime.has_value()) {
    filestat_rounded.mtime = TruncateTimeToSeconds(*filestat.mtime);
  }
  {
    AUTO_SHARED_LOCK(lock, &rwlock_);
    const absl::Time now_truncated = TruncateTimeToSeconds(absl::Now());
    auto iter = dir_entries_cache_.find(path);
    if (iter != dir_entries_cache_.end() &&
        !filestat_rounded.CanBeNewerThan(iter->second.first, now_truncated)) {
      GOMA_COUNTERZ("hit");
      hit_.Add(1);
      *entries = iter->second.second;
      return true;
    }
  }
  GOMA_COUNTERZ("miss");
  miss_.Add(1);

  if (!ListDirectory(path, entries)) {
    return false;
  }

  AUTO_EXCLUSIVE_LOCK(lock, &rwlock_);
  auto exist_entry = dir_entries_cache_.find(path);
  if (exist_entry != dir_entries_cache_.end()) {
    current_entries_ -= exist_entry->second.second.size();
  }

  dir_entries_cache_.emplace_back(path,
                                  std::make_pair(filestat_rounded, *entries));
  current_entries_ += entries->size();

  while (current_entries_ > max_entries_) {
    current_entries_ -= dir_entries_cache_.begin()->second.second.size();
    dir_entries_cache_.pop_front();
  }

  return true;
}

}  // namespace devtools_goma
