// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "filename_id_table.h"

#include <algorithm>

#include "glog/logging.h"
#include "prototmp/deps_cache_data.pb.h"

using std::string;

namespace devtools_goma {

const FilenameIdTable::Id FilenameIdTable::kInvalidId = -1;

FilenameIdTable::FilenameIdTable() :
    next_available_id_(0) {
}

size_t FilenameIdTable::Size() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return map_to_filename_.size();
}

void FilenameIdTable::Clear() {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  ClearUnlocked();
}

void FilenameIdTable::ClearUnlocked() {
  map_to_filename_.clear();
  map_to_id_.clear();
  next_available_id_ = 0;
}

bool FilenameIdTable::LoadFrom(
    const GomaFilenameIdTable& table,
    std::unordered_set<FilenameIdTable::Id>* valid_ids) {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);

  for (const auto& record : table.record()) {
    if (!InsertEntryUnlocked(record.filename(), record.filename_id())) {
      LOG(WARNING) << "Invalid filename_id entry detected: "
                   << record.filename() << " " << record.filename_id();
      ClearUnlocked();
      if (valid_ids) {
        valid_ids->clear();
      }
      return false;
    }

    if (valid_ids)
      valid_ids->insert(record.filename_id());
  }

  return true;
}

void FilenameIdTable::SaveTo(const std::set<FilenameIdTable::Id>& ids,
                             GomaFilenameIdTable* table) const {
  AUTO_SHARED_LOCK(lock, &mu_);

  for (const auto& entry : map_to_filename_) {
    FilenameIdTable::Id id = entry.first;
    const string& filename = entry.second;

    if (!ids.count(id))
      continue;

    GomaFilenameIdTableRecord* record = table->add_record();
    record->set_filename_id(id);
    record->set_filename(filename);
  }
}

bool FilenameIdTable::InsertEntryUnlocked(const string& filename,
                                           FilenameIdTable::Id id) {
  if (id < 0 || filename.empty())
    return false;

  auto it_to_filename = map_to_filename_.find(id);
  if (it_to_filename != map_to_filename_.end() &&
      it_to_filename->second != filename) {
    return false;
  }

  auto it_to_id = map_to_id_.find(filename);
  if (it_to_id != map_to_id_.end() && it_to_id->second != id)
    return false;

  map_to_filename_[id] = filename;
  map_to_id_[filename] = id;
  next_available_id_ = std::max(next_available_id_, id + 1);
  return true;
}

FilenameIdTable::Id FilenameIdTable::InsertFilename(const string& filename) {
  if (filename.empty())
    return kInvalidId;

  {
    AUTO_SHARED_LOCK(lock, &mu_);
    Id id = LookupIdUnlocked(filename);
    if (id != kInvalidId) {
      return id;
    }
  }

  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  Id id = LookupIdUnlocked(filename);
  if (id != kInvalidId) {
    return id;
  }

  map_to_id_[filename] = next_available_id_;
  map_to_filename_[next_available_id_] = filename;
  return next_available_id_++;
}

FilenameIdTable::Id FilenameIdTable::LookupIdUnlocked(
    const string& filename) const {
  auto it = map_to_id_.find(filename);
  if (it == map_to_id_.end())
    return kInvalidId;
  return it->second;
}

string FilenameIdTable::ToFilename(FilenameIdTable::Id id) const {
  AUTO_SHARED_LOCK(lock, &mu_);
  auto it = map_to_filename_.find(id);
  if (it == map_to_filename_.end())
    return string();
  return it->second;
}

FilenameIdTable::Id FilenameIdTable::ToId(const string& filename) const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return LookupIdUnlocked(filename);
}

}  // namespace devtools_goma
