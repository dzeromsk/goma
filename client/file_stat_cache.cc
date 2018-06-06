// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "file_stat_cache.h"

#include <string>

#include <glog/logging.h>

#include "autolock_timer.h"
#include "counterz.h"
#include "path.h"

using std::string;

namespace devtools_goma {

// TODO: Add stats.

FileStat GlobalFileStatCache::Get(const string& path) {
  {
    AUTO_SHARED_LOCK(lock, &mu_);
    auto it = file_stats_.find(path);
    if (it != file_stats_.end()) {
      return it->second;
    }
  }

  FileStat id(path);
  if (!id.IsValid() || id.is_directory) {
    return id;
  }

  {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    file_stats_.emplace(path, id);
  }
  return id;
}

GlobalFileStatCache* GlobalFileStatCache::instance_ = nullptr;

/* static */
void GlobalFileStatCache::Init() {
  CHECK(instance_ == nullptr);
  instance_ = new GlobalFileStatCache;
}

/* static */
void GlobalFileStatCache::Quit() {
  CHECK(instance_ != nullptr);
  delete instance_;
  instance_ = nullptr;
}

/* static */
GlobalFileStatCache* GlobalFileStatCache::Instance() {
  return instance_;
}

FileStatCache::FileStatCache()
    : is_acquired_(true), owner_thread_id_(GetCurrentThreadId()) {}

FileStatCache::~FileStatCache() {
  DCHECK(!is_acquired_ || THREAD_ID_IS_SELF(owner_thread_id_));
}

FileStat FileStatCache::Get(const string& filename) {
  GOMA_COUNTERZ("Get");

  DCHECK(is_acquired_ && THREAD_ID_IS_SELF(owner_thread_id_));
  DCHECK(file::IsAbsolutePath(filename)) << filename;

  FileStatMap::iterator iter = file_stats_.find(filename);
  if (iter != file_stats_.end())
    return iter->second;

  FileStat id;

  if (GlobalFileStatCache::Instance() != nullptr) {
    id = GlobalFileStatCache::Instance()->Get(filename);
  } else {
    id = FileStat(filename);
  }

  file_stats_.insert(std::make_pair(filename, id));
  VLOG(2) << filename << " " << id.DebugString();

  return id;
}

void FileStatCache::Clear() {
  DCHECK(is_acquired_ && THREAD_ID_IS_SELF(owner_thread_id_));
  file_stats_.clear();
}

void FileStatCache::AcquireOwner() {
  DCHECK(!is_acquired_);
  is_acquired_ = true;
  owner_thread_id_ = GetCurrentThreadId();
}

void FileStatCache::ReleaseOwner() {
  DCHECK(is_acquired_ && THREAD_ID_IS_SELF(owner_thread_id_));
  is_acquired_ = false;
}

}  // namespace devtools_goma
