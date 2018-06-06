// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_DEPS_CACHE_H_
#define DEVTOOLS_GOMA_CLIENT_DEPS_CACHE_H_

#include <atomic>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "absl/types/optional.h"
#include "autolock_timer.h"
#include "cache_file.h"
#include "file_stat_cache.h"
#include "filename_id_table.h"
#include "goma_hash.h"
#include "sha256hash_hasher.h"

namespace devtools_goma {

class CompilerFlags;
class CompilerInfo;
class DepsCacheStats;

// DepsCache is a cache for dependent files.
// We make an 'identifier' which identifies compile command,
// and a map from 'identifier' to dependent files (and extra information).
//
// When we run the same command which has the same identifier,
// we check whether we can reuse the dependet files list.
// This is done by the following algorithm:
//  For all dependent files:
//   1. Check FileStat. If it's the same, we think a file is not changed.
//   2. Check directive_hash, which is a hash value created from file's
//      directive lines. If it's the same, dependant files won't be changed.
class DepsCache {
 public:
  using Identifier = absl::optional<SHA256HashValue>;

  static DepsCache* instance() { return instance_; }
  static bool IsEnabled() { return instance_ != nullptr; }

  // Initializes the DepsCache.
  // When |cache_filename| is empty, this won't be enabled.
  // When |cache_filename| file exists, we load it.
  static void Init(const std::string& cache_filename,
                   int identifier_alive_duration,
                   size_t deps_table_size_threshold,
                   int max_proto_size_in_mega_bytes);

  // Saves .goma_deps file is DepsCache is initialized.
  static void Quit();

  // Creates identifier to set/get dependencies.
  static Identifier MakeDepsIdentifier(
      const CompilerInfo& compiler_info,
      const CompilerFlags& compiler_flags);

  // Records a dependency; a compile command can be identified with
  // |identifier|, and the command uses |input_file| as
  // an input file (e.g. *.cc), also, the command requires
  // |dependencies| files (e.g. *.h), can be relative.
  // |cwd| should be absolute.
  // |identifier| should not be empty.
  // |input_file| can be relative.
  bool SetDependencies(const Identifier& identifier,
                       const std::string& cwd,
                       const std::string& input_file,
                       const std::set<std::string>& dependencies,
                       FileStatCache* file_stat_cache);

  // Gets dependent files using |identifer|.
  // We check the dependecies are not changed. If changed, false will be
  // returned and |dependecies| won't be changed.
  // |input_file| is removed from the result of |dependencies|.
  // |input_file| can be relative.
  // |cwd| should be absolute.
  // path in |dependencies| can be relative.
  bool GetDependencies(const Identifier& identifier,
                       const std::string& cwd,
                       const std::string& input_file,
                       std::set<std::string>* dependencies,
                       FileStatCache* file_stat_cache);

  void RemoveDependency(const Identifier& identifier);

  // Dump internal stats.
  void DumpStatsToProto(DepsCacheStats* stats) const;

 private:
  friend class DepsCacheTest;

  // DepsHashId is used to check whether an include file is updated.
  // |directive_hash| is a hash value of the file's directive lines.
  struct DepsHashId {
    DepsHashId() {}
    DepsHashId(FilenameIdTable::Id id,
               const FileStat& file_stat,
               const SHA256HashValue& directive_hash)
        : id(id), file_stat(file_stat), directive_hash(directive_hash) {}

    bool IsValid() const {
      return id != FilenameIdTable::kInvalidId && file_stat.IsValid();
    }

    FilenameIdTable::Id id;
    FileStat file_stat;
    SHA256HashValue directive_hash;
  };

  struct DepsTableData {
    DepsTableData() : last_used_time(0) {
    }

    std::atomic<time_t> last_used_time;
    std::vector<DepsHashId> deps_hash_ids;
  };

  typedef SHA256HashValue Key;
  typedef std::unordered_map<Key, DepsTableData, SHA256HashValueHasher>
      DepsTable;

  DepsCache(const string& cache_filename,
            int identifier_alive_duration,
            size_t deps_table_size_threshold,
            int max_proto_size_in_mega_bytes);
  ~DepsCache();

  void Clear();

  bool SaveGomaDeps();
  bool LoadGomaDeps();

  void IncrMissedCount();
  void IncrMissedByUpdatedCount();
  void IncrHitCount();

  static bool IsDirectiveModified(const string& filename,
                                  const FileStat& old_file_stat,
                                  const SHA256HashValue& old_directive_hash,
                                  FileStatCache* file_stat_cache);

  // Used for test.
  bool UpdateLastUsedTime(const Identifier& identifier, time_t last_used_time);
  size_t deps_table_size() const {
    AUTO_SHARED_LOCK(lock, &mu_);
    return deps_table_.size();
  }
  bool GetDepsHashId(const Identifier& identifier,
                     const FilenameIdTable::Id& id,
                     DepsHashId* deps_hash_id) const;

  static DepsCache* instance_;

  const CacheFile cache_file_;
  // When an identifier is older than this value (in second), it won't be
  // removed in save/load. If negative, we don't dispose old cache.
  const int identifier_alive_duration_;
  // When lots of DepsTable exist, we'd like to remove older DepsTable entry
  // when saving.
  const size_t deps_table_size_threshold_;
  // If the proto for cache exceeds this size, loading will fail.
  // In that case, cache is just ignored.
  const int max_proto_size_in_mega_bytes_;

  mutable ReadWriteLock mu_;
  DepsTable deps_table_ GUARDED_BY(mu_);

  // Instead of using a filename, we alternatively use an id for
  // performance and memory space. So, we manage this table to convert
  // between filename and id.
  FilenameIdTable filename_id_table_;

  mutable Lock count_mu_;
  unsigned int hit_count_ GUARDED_BY(count_mu_);
  unsigned int missed_count_ GUARDED_BY(count_mu_);
  unsigned int missed_by_updated_count_ GUARDED_BY(count_mu_);

  DISALLOW_COPY_AND_ASSIGN(DepsCache);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_DEPS_CACHE_H_
