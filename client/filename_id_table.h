// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#ifndef DEVTOOLS_GOMA_CLIENT_FILENAME_ID_TABLE_H_
#define DEVTOOLS_GOMA_CLIENT_FILENAME_ID_TABLE_H_

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "autolock_timer.h"

namespace devtools_goma {

class GomaFilenameIdTable;

// FilenameIdTable converts filepath <-> integer id.
// The instance of this class is thread-safe.
class FilenameIdTable {
 public:
  typedef int Id;

  static const Id kInvalidId;

  FilenameIdTable();

  size_t Size() const;

  // Clears all data.
  void Clear();

  // Loads the data from |table|. If loading failed (because of duplicated
  // entry etc.), false will be returned.
  // |valid_ids| will contain all the valid ids if not null.
  bool LoadFrom(const GomaFilenameIdTable& table,
                std::unordered_set<FilenameIdTable::Id>* valid_ids);
  // Saves the data to |table|. Only entry that has |ids| will be saved.
  void SaveTo(const std::set<Id>& ids, GomaFilenameIdTable* table) const;

  // Inserts |filename|.
  // If |filename| is a new one, a new Id will be returned.
  // If |filename| is already inserted, the corresponding Id is returned.
  // If |filename| is empty, kInvalidId is returned.
  Id InsertFilename(const std::string& filaname);

  // Converts |id| to filaname. If |id| is not registered, empty string will
  // be returned.
  std::string ToFilename(Id id) const;

  // Converts |filename| to Id. If |filename| is not registered,
  // kInvalidId is returned.
  Id ToId(const std::string& filename) const;

 private:
  bool InsertEntryUnlocked(const std::string& filename, Id id)
      EXCLUSIVE_LOCKS_REQUIRED(mu_);

  void ClearUnlocked()
      EXCLUSIVE_LOCKS_REQUIRED(mu_);

  Id LookupIdUnlocked(const std::string& filename) const
      SHARED_LOCKS_REQUIRED(mu_);

  mutable ReadWriteLock mu_;
  Id next_available_id_ GUARDED_BY(mu_);
  std::unordered_map<Id, std::string> map_to_filename_ GUARDED_BY(mu_);
  std::unordered_map<std::string, Id> map_to_id_ GUARDED_BY(mu_);

  DISALLOW_COPY_AND_ASSIGN(FilenameIdTable);
};

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_FILENAME_ID_TABLE_H_
