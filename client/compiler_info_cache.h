// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_CACHE_H_

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "absl/time/time.h"
#include "autolock_timer.h"
#include "basictypes.h"
#include "cache_file.h"
#include "compiler_info.h"
#include "json/json.h"
#include "lockhelper.h"

namespace devtools_goma {

class CompilerFlags;
class CompilerInfo;
class CompilerInfoState;
class CompilerInfoDataTable;

// CompilerInfoCache caches CompilerInfo.
// Information about a particular compiler found in 'path', with
// extra '-mxx' information.
// This class is thread-safe.
class CompilerInfoCache {
 public:
  struct Key {
    static const bool kCwdRelative = true;
    std::string base;
    std::string cwd;
    std::string local_compiler_path;

    std::string ToString(bool cwd_relative) const;
    std::string abs_local_compiler_path() const;
  };

  // CompilerInfoValidator just calls IsValid() of CompilerInfo.
  // You can set your own validator to test CompilerInfoCache.
  class CompilerInfoValidator {
  public:
    virtual ~CompilerInfoValidator() {}
    // Returns true if compiler_info cache is valid.
    virtual bool Validate(const CompilerInfo& compiler_info,
                          const std::string& local_compiler_path);
  };

  ~CompilerInfoCache();

  // Initializes the CompilerInfoCache.
  // when cache_filename is empty, this won't load cached data.
  // otherwise, it will try to load cached data from
  // JoinPathRespectAbsolute(cache_dir, cache_filename).
  static void Init(const string& cache_dir, const string& cache_filename,
                   absl::Duration cache_holding_time);
  static CompilerInfoCache* instance() { return instance_; }

  // Saves CompilerInfoCache into cache file.
  static void Quit();

  static Key CreateKey(const CompilerFlags& flags,
                       const std::string& local_compiler_path,
                       const std::vector<std::string>& key_envs);

  // Lookup just checks cached compiler_info.
  // Returns CompilerInfoState in cache.
  // It would be better to use ScopedCompilerInfoState to manage the
  // returned pointer.
  //    ScopedCompilerInfoState cis(cache->Lookup(...));
  //
  // Note that found compiler_info may not be valid.
  // Returns nullptr if it missed in cache or found obsoleted.
  CompilerInfoState* Lookup(const Key& key);

  // Store stores compiler_info in cache and returns compiler_info_state.
  // compiler_info may be disabled if the same local compiler was already
  // disabled.
  // Takes ownership of data.
  CompilerInfoState* Store(const Key& key,
                           std::unique_ptr<CompilerInfoData> data);

  // Disable compiler_info_state and other compiler_info_states with
  // the same local compiler.
  bool Disable(CompilerInfoState* compiler_info_state,
               const std::string& disabled_reason);

  void Dump(std::ostringstream* ss);
  void DumpCompilersJSON(Json::Value* json);

  bool HasCompilerMismatch() const;

  int NumStores() const;
  int NumStoreDups() const;
  int NumMiss() const;
  int NumFail() const;
  int LoadedSize() const;

  // Takes the ownership of validator.
  // Use this for testing purpose.
  void SetValidator(CompilerInfoValidator* validator) LOCKS_EXCLUDED(mu_);
  CompilerInfoValidator* validator() const LOCKS_EXCLUDED(mu_) {
    AUTO_SHARED_LOCK(lock, &mu_);
    return validator_.get();
  }

  bool Save() LOCKS_EXCLUDED(mu_);

 private:
  CompilerInfoCache(const string& cache_filename,
                    absl::Duration cache_holding_time);

  static string HashKey(const CompilerInfoData& data);
  bool Load() LOCKS_EXCLUDED(mu_);
  bool Unmarshal(const CompilerInfoDataTable& table) LOCKS_EXCLUDED(mu_);
  bool UnmarshalUnlocked(const CompilerInfoDataTable& table)
      EXCLUSIVE_LOCKS_REQUIRED(mu_);
  bool Marshal(CompilerInfoDataTable* table) LOCKS_EXCLUDED(mu_);
  bool MarshalUnlocked(CompilerInfoDataTable* table) SHARED_LOCKS_REQUIRED(mu_);
  void Clear() LOCKS_EXCLUDED(mu_);
  void ClearUnlocked() EXCLUSIVE_LOCKS_REQUIRED(mu_);

  CompilerInfoState* LookupUnlocked(const string& compiler_info_key,
                                    const string& abs_local_compiler_path)
      SHARED_LOCKS_REQUIRED(mu_);

  // Check CompilerInfo validity. CompilerInfo that does not match with the
  // current local compiler will be removed or updated.
  void UpdateOlderCompilerInfo() LOCKS_EXCLUDED(mu_);
  void UpdateOlderCompilerInfoUnlocked() EXCLUSIVE_LOCKS_REQUIRED(mu_);

  friend class CompilerInfoCacheTest;

  static CompilerInfoCache* instance_;

  const CacheFile cache_file_;
  const absl::Duration cache_holding_time_;

  std::unique_ptr<CompilerInfoValidator> validator_ GUARDED_BY(mu_);

  mutable ReadWriteLock mu_;

  // key: compiler_info_key
  std::unordered_map<std::string, CompilerInfoState*> compiler_info_
      GUARDED_BY(mu_);

  // key: hash of CompilerInfoData. value: compiler_info_key.
  std::unordered_map<std::string,
                     std::unique_ptr<std::unordered_set<std::string>>>
      keys_by_hash_ GUARDED_BY(mu_);

  int num_stores_ GUARDED_BY(mu_);
  int num_store_dups_ GUARDED_BY(mu_);
  int num_miss_ GUARDED_BY(mu_);
  int num_fail_ GUARDED_BY(mu_);
  int loaded_size_ GUARDED_BY(mu_);

  DISALLOW_COPY_AND_ASSIGN(CompilerInfoCache);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_COMPILER_INFO_CACHE_H_
