// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "file_id_cache.h"

#include <string>

#include <glog/logging.h>

#include "autolock_timer.h"
#include "counterz.h"
#include "path.h"

using std::string;

namespace devtools_goma {

// TODO: Add stats.

FileId GlobalFileIdCache::Get(const string& path) {
  {
    AUTO_SHARED_LOCK(lock, &mu_);
    auto it = file_ids_.find(path);
    if (it != file_ids_.end()) {
      return it->second;
    }
  }

  FileId id(path);
  if (!id.IsValid() || id.is_directory) {
    return id;
  }

  {
    AUTO_EXCLUSIVE_LOCK(lock, &mu_);
    file_ids_.emplace(path, id);
  }
  return id;
}

GlobalFileIdCache* GlobalFileIdCache::instance_ = nullptr;

/* static */
void GlobalFileIdCache::Init() {
  CHECK(instance_ == nullptr);
  instance_ = new GlobalFileIdCache;
}

/* static */
void GlobalFileIdCache::Quit() {
  CHECK(instance_ != nullptr);
  delete instance_;
  instance_ = nullptr;
}

/* static */
GlobalFileIdCache* GlobalFileIdCache::Instance() {
  return instance_;
}

FileIdCache::FileIdCache()
    : is_acquired_(true), owner_thread_id_(GetCurrentThreadId()) {
}

FileIdCache::~FileIdCache() {
  DCHECK(!is_acquired_ || THREAD_ID_IS_SELF(owner_thread_id_));
}

FileId FileIdCache::Get(const string& filename) {
  GOMA_COUNTERZ("Get");

  DCHECK(is_acquired_ && THREAD_ID_IS_SELF(owner_thread_id_));
  DCHECK(file::IsAbsolutePath(filename)) << filename;

  FileIdMap::iterator iter = file_ids_.find(filename);
  if (iter != file_ids_.end())
    return iter->second;

  FileId id;

  if (GlobalFileIdCache::Instance() != nullptr) {
    id = GlobalFileIdCache::Instance()->Get(filename);
  } else {
    id = FileId(filename);
  }

  file_ids_.insert(std::make_pair(filename, id));

  return id;
}

void FileIdCache::Clear() {
  DCHECK(is_acquired_ && THREAD_ID_IS_SELF(owner_thread_id_));
  file_ids_.clear();
}

void FileIdCache::AcquireOwner() {
  DCHECK(!is_acquired_);
  is_acquired_ = true;
  owner_thread_id_ = GetCurrentThreadId();
}

void FileIdCache::ReleaseOwner() {
  DCHECK(is_acquired_ && THREAD_ID_IS_SELF(owner_thread_id_));
  is_acquired_ = false;
}

}  // namespace devtools_goma
