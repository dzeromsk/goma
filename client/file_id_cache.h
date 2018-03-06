// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILE_ID_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_FILE_ID_CACHE_H_

#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "basictypes.h"
#include "file_id.h"
#include "lockhelper.h"
#include "platform_thread.h"

using std::string;

namespace devtools_goma {

// GlobalFileIdCache caches FileIds globally.
// This only holds valid and non-directory FileIds.
// The instance of this class is thread-safe.
class GlobalFileIdCache {
 public:
  FileId Get(const string& path);

  static void Init();
  static void Quit();
  static GlobalFileIdCache* Instance();

 private:
  mutable ReadWriteLock mu_;
  std::unordered_map<string, FileId> file_ids_ GUARDED_BY(mu_);

  static GlobalFileIdCache* instance_;
};

// FileIdCache caches FileIds.
// Instance of this class is thread-unsafe.
class FileIdCache {
 public:
  FileIdCache();
  ~FileIdCache();

  // Returns FileId cache if any. If not, we create FileId for |filename|.
  FileId Get(const string& filename);

  // Clears all caches.
  void Clear();

  // Caller thread takes ownership of the instance of FileIdCache.
  void AcquireOwner();

  // Caller thread releases ownership of the instance of FileIdCache.
  void ReleaseOwner();

  friend class DepsCacheTest;

 private:
  typedef std::unordered_map<string, FileId> FileIdMap;

  bool is_acquired_;
  PlatformThreadId owner_thread_id_;

  FileIdMap file_ids_;

  DISALLOW_COPY_AND_ASSIGN(FileIdCache);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FILE_ID_CACHE_H_
