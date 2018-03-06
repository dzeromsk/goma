// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_info_cache.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "absl/strings/str_join.h"
#include "autolock_timer.h"
#include "compiler_flags.h"
#include "compiler_proxy_info.h"
#include "file.h"
#include "glog/logging.h"
#include "goma_hash.h"
#include "path.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/compiler_info_data.pb.h"
MSVC_POP_WARNING()

using std::string;

namespace devtools_goma {

const int kNegativeCacheDurationSec = 600;  // 10 minutes.
const int kUpdateLastUsedAtDurationSec = 600;  // 10 minutes.

CompilerInfoCache* CompilerInfoCache::instance_;

string CompilerInfoCache::Key::ToString(bool cwd_relative) const {
  if (cwd_relative) {
    return local_compiler_path + " " + base + cwd;
  }
  // if |local_compiler_path| is not absolute path,
  // CompilerInfo may not be independent of |cwd|.
  // e.g. with -no-canonical-prefixes
  DCHECK(file::IsAbsolutePath(local_compiler_path));
  return local_compiler_path + " " + base;
}

string CompilerInfoCache::Key::abs_local_compiler_path() const {
  return file::JoinPathRespectAbsolute(cwd, local_compiler_path);
}

/* static */
void CompilerInfoCache::Init(const string& cache_dir,
                             const string& cache_filename,
                             int cache_holding_time_sec) {
  CHECK(instance_ == nullptr);
  if (cache_filename == "") {
    instance_ = new CompilerInfoCache("", cache_holding_time_sec);
    return;
  }
  instance_ = new CompilerInfoCache(
      file::JoinPathRespectAbsolute(cache_dir, cache_filename),
      cache_holding_time_sec);
}

void CompilerInfoCache::Quit() {
  delete instance_;
  instance_ = nullptr;
}

CompilerInfoCache::CompilerInfoCache(const string& cache_filename,
                                     int cache_holding_time_sec)
    : cache_file_(cache_filename),
      cache_holding_time_sec_(cache_holding_time_sec),
      validator_(new CompilerInfoCache::CompilerInfoValidator),
      num_stores_(0),
      num_store_dups_(0),
      num_miss_(0),
      num_fail_(0),
      loaded_size_(0) {
  if (cache_file_.Enabled()) {
    Load();
  } else {
    LOG(INFO) << "compiler_info_cache: no cache file";
  }
}

CompilerInfoCache::~CompilerInfoCache() {
  if (cache_file_.Enabled()) {
    Save();
  }
  Clear();
}

void CompilerInfoCache::Clear() {
  for (auto& it : keys_by_hash_) {
    delete it.second;
  }
  keys_by_hash_.clear();
  for (auto& it : compiler_info_) {
    it.second->Deref();
  }
  compiler_info_.clear();
}

/* static */
CompilerInfoCache::Key CompilerInfoCache::CreateKey(
    const CompilerFlags& flags,
    const std::string& local_compiler_path,
    const std::vector<std::string>& key_envs) {
  const std::vector<string>& compiler_info_flags = flags.compiler_info_flags();
  std::vector<string> compiler_info_keys(compiler_info_flags);
  copy(key_envs.begin(), key_envs.end(), back_inserter(compiler_info_keys));
  string compiler_info_keys_str = absl::StrJoin(compiler_info_keys, " ");

  Key key;
  key.base = compiler_info_keys_str + " lang:" + flags.lang() + " @";
  key.cwd = flags.cwd();
  key.local_compiler_path = local_compiler_path;
  return key;
}

CompilerInfoState* CompilerInfoCache::Lookup(const Key& key) {
  AUTO_SHARED_LOCK(lock, &mu_);
  CompilerInfoState* state = nullptr;
  if (file::IsAbsolutePath(key.local_compiler_path)) {
    state = LookupUnlocked(key.ToString(!Key::kCwdRelative),
                           key.local_compiler_path);
  }
  if (state == nullptr) {
    state = LookupUnlocked(key.ToString(Key::kCwdRelative),
                           key.abs_local_compiler_path());
  }

  // Update last used timestamp of |state| having old timestamp.
  if (state != nullptr &&
      time(nullptr) - state->info().last_used_at() >
      kUpdateLastUsedAtDurationSec) {
    state->UpdateLastUsedAt();
  }

  return state;
}

CompilerInfoState* CompilerInfoCache::LookupUnlocked(
    const string& compiler_info_key,
    const string& abs_local_compiler_path) {
  auto it = compiler_info_.find(compiler_info_key);
  if (it == compiler_info_.end()) {
    return nullptr;
  }
  auto info = it->second;
  if (validator_->Validate(info->info(), abs_local_compiler_path)) {
    VLOG(1) << "Cache hit for compiler-info with key: "
            << compiler_info_key;

    if (!info->info().HasError()) {
      return info;
    }

    time_t now = time(nullptr);
    if (now < info->info().failed_at() + kNegativeCacheDurationSec) {
      return info;
    }

    VLOG(1) << "Negative cache is expired: " << compiler_info_key;
  }

  LOG(INFO) << "Cache hit, but obsolete compiler-info for key: "
            << compiler_info_key;
  return nullptr;
}

CompilerInfoState* CompilerInfoCache::Store(
    const Key& key, std::unique_ptr<CompilerInfoData> data) {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);
  DCHECK(data != nullptr);

  ScopedCompilerInfoState state;

  bool dup = false;
  string dup_compiler_info_key;
  string hash = HashKey(*data);
  {
    auto found = keys_by_hash_.find(hash);
    if (found != keys_by_hash_.end()) {
      std::unordered_set<string>* keys = found->second;
      if (!keys->empty()) {
        const string& compiler_info_key = *keys->begin();
        state.reset(LookupUnlocked(
            compiler_info_key, key.abs_local_compiler_path()));
        if (state.get() != nullptr) {
          LOG(INFO) << "hash=" << hash << " share with " << compiler_info_key;
          dup = true;
        }
      }
    }
  }

  if (state.get() == nullptr) {
    state.reset(new CompilerInfoState(std::move(data)));
  }
  state.get()->Ref();  // in cache.

  if (!state.get()->info().found()) {
    ++num_miss_;
    DCHECK(state.get()->info().HasError());
    DCHECK_NE(state.get()->info().failed_at(), 0);
  } else if (state.get()->info().HasError()) {
    ++num_fail_;
    DCHECK_NE(state.get()->info().failed_at(), 0);
  } else if (dup) {
    ++num_store_dups_;
    DCHECK_EQ(state.get()->info().failed_at(), 0);
  } else {
    ++num_stores_;
    DCHECK_EQ(state.get()->info().failed_at(), 0);
  }

  string old_hash;
  const string compiler_info_key =
      key.ToString(!file::IsAbsolutePath(key.local_compiler_path) ||
                   state.get()->info().IsCwdRelative(key.cwd));
  {
    auto p = compiler_info_.insert(
        std::make_pair(compiler_info_key, state.get()));
    if (!p.second) {
      CompilerInfoState* old_state = p.first->second;
      old_hash = HashKey(old_state->info().data());
      old_state->Deref();
      p.first->second = state.get();
    }
  }
  {
    std::unordered_set<string>* keys = nullptr;
    auto p = keys_by_hash_.insert(std::make_pair(hash, keys));
    if (p.second) {
      p.first->second = new std::unordered_set<string>;
    }
    p.first->second->insert(compiler_info_key);
    LOG(INFO) << "hash=" << hash << " key=" << compiler_info_key;
  }
  if (old_hash != "") {
    auto p = keys_by_hash_.find(old_hash);
    if (p != keys_by_hash_.end()) {
      LOG(INFO) << "delete hash=" << hash << " key=" << compiler_info_key;
      p->second->erase(compiler_info_key);
      if (p->second->empty()) {
        LOG(INFO) << "delete hash=" << hash;
        delete p->second;
        keys_by_hash_.erase(p);
      }
    }
  }
  if (dup) {
    DCHECK_GT(state.get()->refcnt(), 2);
  } else {
    DCHECK_EQ(state.get()->refcnt(), 2);
  }
  LOG(INFO) << "Update state=" << state.get()
            << " for key=" << compiler_info_key
            << " hash=" << hash;

  // Check if the same local compiler was already disabled.
  for (const auto& info : compiler_info_) {
    CompilerInfoState* cis = info.second;
    if (!cis->disabled())
      continue;
    if (state.get()->info().IsSameCompiler(cis->info())) {
      state.get()->SetDisabled(true, "the same compiler is already disabled");
      LOG(INFO) << "Disabled state=" << state.get();
      break;
    }
  }
  // CompilerInfoState is referenced in cache, so it won't be destroyed
  // when state is destroyed.
  return state.get();
}

bool CompilerInfoCache::Disable(CompilerInfoState* compiler_info_state,
                                const std::string& disabled_reason) {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);

  LOG(INFO) << "Disable state=" << compiler_info_state;
  bool disabled = false;
  if (!compiler_info_state->disabled()) {
    compiler_info_state->SetDisabled(true, disabled_reason);
    LOG(INFO) << "Disabled state=" << compiler_info_state;
    disabled = true;
  }

  // Also mark other CompilerInfo disabled if it is the same
  // local compiler (but it would use different compiler_info_flags).
  for (auto& info : compiler_info_) {
    CompilerInfoState* cis = info.second;
    if (cis->disabled())
      continue;
    if (compiler_info_state->info().IsSameCompiler(cis->info())) {
      if (!cis->disabled()) {
        cis->SetDisabled(true, disabled_reason);
        LOG(INFO) << "Disabled state=" << cis;
      }
    }
  }

  return disabled;
}

void CompilerInfoCache::Dump(std::ostringstream* ss) {
  AUTO_SHARED_LOCK(lock, &mu_);
  (*ss) << "compiler info:" << compiler_info_.size()
        << " info_hashes=" << keys_by_hash_.size() << "\n";

  (*ss) << "\n[keys by hash]\n";
  for (const auto& it : keys_by_hash_) {
    (*ss) << "hash: " << it.first << "\n";
    for (const auto& k : *it.second) {
      (*ss) << " key:" << k << "\n";
    }
    (*ss) << "\n";
  }
  (*ss) << "\n";

  (*ss) << "\n[compiler info]\n\n";
  for (const auto& info : compiler_info_) {
    (*ss) << "key: " << info.first;
    (*ss) << "\n";
    if (info.second->disabled()) {
      (*ss) << "disabled ";
    }
    (*ss) << "state=" << info.second;
    (*ss) << " cnt=" << info.second->refcnt();
    (*ss) << " used=" << info.second->used();
    (*ss) << "\n";
    (*ss) << info.second->info().DebugString() << "\n";
  }
}

// Dump compiler itself information (not CompilerInfo).
// For each one compiler, only one entry is dumped.
void CompilerInfoCache::DumpCompilersJSON(Json::Value* json) {
  AUTO_SHARED_LOCK(lock, &mu_);

  // Dumping whole CompilerInfoData could be too large, and
  // it is not compiler itself information but CompilerInfo.
  // So, we extract a few fields from CompilerInfoData.

  Json::Value arr(Json::arrayValue);

  std::unordered_set<std::string> used;
  for (const auto& info : compiler_info_) {
    const CompilerInfoData& data = info.second->info().data();

    // Check local_compiler_path so that the same compiler does not appear
    // twice.
    if (used.count(data.local_compiler_path()) > 0) {
      continue;
    }
    used.insert(data.local_compiler_path());

    Json::Value value;
    value["name"] = data.name();
    value["version"] = data.version();
    value["target"] = data.target();

    value["local_compiler_path"] = data.local_compiler_path();
    value["local_compiler_hash"] = data.local_compiler_hash();

    value["real_compiler_path"] = data.real_compiler_path();
    value["real_compiler_hash"] = data.hash();  // hash() is real compiler hash.

    arr.append(std::move(value));
  }

  (*json)["compilers"] = std::move(arr);
}

bool CompilerInfoCache::HasCompilerMismatch() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  for (const auto& info : compiler_info_) {
    if (info.second->disabled())
      return true;
  }
  return false;
}

int CompilerInfoCache::NumStores() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return num_stores_;
}

int CompilerInfoCache::NumStoreDups() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return num_store_dups_;
}

int CompilerInfoCache::NumMiss() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return num_miss_;
}

int CompilerInfoCache::NumFail() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return num_fail_;
}

int CompilerInfoCache::LoadedSize() const {
  AUTO_SHARED_LOCK(lock, &mu_);
  return loaded_size_;
}

void CompilerInfoCache::SetValidator(CompilerInfoValidator* validator) {
  CHECK(validator);
  validator_.reset(validator);
}

bool CompilerInfoCache::CompilerInfoValidator::Validate(
    const CompilerInfo& compiler_info,
    const string& local_compiler_path) {
  return compiler_info.IsUpToDate(local_compiler_path);
}

/* static */
string CompilerInfoCache::HashKey(const CompilerInfoData& data) {
  string serialized;
  data.SerializeToString(&serialized);
  string hash;
  ComputeDataHashKey(serialized, &hash);
  return hash;
}

bool CompilerInfoCache::Load() {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);

  LOG(INFO) << "loading from " << cache_file_.filename();

  CompilerInfoDataTable table;
  if (!cache_file_.Load(&table)) {
    LOG(ERROR) << "failed to load cache file " << cache_file_.filename();
    return false;
  }

  Unmarshal(table);
  if (table.built_revision() != kBuiltRevisionString) {
    LOG(WARNING) << "loaded from " << cache_file_.filename()
                 << " mismatch built_revision: got=" << table.built_revision()
                 << " want=" << kBuiltRevisionString;
    Clear();
    return false;
  }

  loaded_size_ = table.ByteSize();

  LOG(INFO) << "loaded from " << cache_file_.filename()
            << " loaded size " << loaded_size_;

  UpdateOlderCompilerInfo();

  return true;
}

void CompilerInfoCache::UpdateOlderCompilerInfo() {
  // Check CompilerInfo validity. Obsolete CompilerInfo will be removed.
  // Since calculating sha256 is slow, we need cache. Otherwise, we will
  // need more than 2 seconds to check.
  std::unordered_map<string, string> sha256_cache;
  std::vector<string> keys_to_remove;
  time_t now = time(nullptr);

  for (const auto& entry : compiler_info_) {
    const std::string& key = entry.first;
    CompilerInfoState* state = entry.second;

    const std::string& abs_local_compiler_path =
        state->compiler_info_.abs_local_compiler_path();

    // if the cache is not used recently, we do not reuse it.
    time_t time_diff = now - state->info().last_used_at();
    if (time_diff > cache_holding_time_sec_) {
      LOG(INFO) << "evict old cache: " << abs_local_compiler_path
                << " last used at: "
                << time_diff / (60 * 60 * 24)
                << " days ago";
      keys_to_remove.push_back(key);
      continue;
    }

    if (validator_->Validate(state->info(), abs_local_compiler_path)) {
      LOG(INFO) << "valid compiler: " << abs_local_compiler_path;
      continue;
    }

    if (state->compiler_info_.UpdateFileIdIfHashMatch(&sha256_cache)) {
      LOG(INFO) << "compiler fileid didn't match, but hash matched: "
                << abs_local_compiler_path;
      continue;
    }

    LOG(INFO) << "compiler outdated: " << abs_local_compiler_path;
    keys_to_remove.push_back(key);
  }

  for (const auto& key : keys_to_remove) {
    LOG(INFO) << "Removing outdated compiler: " << key;
    auto it = compiler_info_.find(key);
    if (it != compiler_info_.end()) {
      it->second->Deref();
      compiler_info_.erase(it);
    }
  }
}

bool CompilerInfoCache::Unmarshal(const CompilerInfoDataTable& table) {
  for (const auto& it : table.compiler_info_data()) {
    std::unordered_set<string>* keys = new std::unordered_set<string>;
    for (const auto& key : it.keys()) {
      keys->insert(key);
    }
    const CompilerInfoData& data = it.data();
    std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
    *cid = data;
    const string& hash = HashKey(*cid);
    ScopedCompilerInfoState state(new CompilerInfoState(std::move(cid)));
    for (const auto& key : *keys) {
      compiler_info_.insert(std::make_pair(key, state.get()));
      state.get()->Ref();
    }
    keys_by_hash_.insert(std::make_pair(hash, keys));
  }
  // TODO: can be void?
  return true;
}

bool CompilerInfoCache::Save() {
  AUTO_EXCLUSIVE_LOCK(lock, &mu_);

  LOG(INFO) << "saving to " << cache_file_.filename();

  CompilerInfoDataTable table;
  if (!Marshal(&table)) {
    return false;
  }

  if (!cache_file_.Save(table)) {
    LOG(ERROR) << "failed to save cache file " << cache_file_.filename();
    return false;
  }
  LOG(INFO) << "saved to " << cache_file_.filename();
  return true;
}

bool CompilerInfoCache::Marshal(CompilerInfoDataTable* table) {
  std::unordered_map<string, CompilerInfoDataTable::Entry*> by_hash;
  for (const auto& it : compiler_info_) {
    const string& info_key = it.first;
    CompilerInfoState* state = it.second;
    if (state->disabled()) {
      continue;
    }
    const CompilerInfoData& data = state->info().data();
    string hash = HashKey(data);
    CompilerInfoDataTable::Entry* entry = nullptr;
    auto p = by_hash.insert(std::make_pair(hash, entry));
    if (p.second) {
      p.first->second = table->add_compiler_info_data();
      p.first->second->mutable_data()->CopyFrom(data);
    }
    entry = p.first->second;
    entry->add_keys(info_key);
  }
  table->set_built_revision(kBuiltRevisionString);
  // TODO: can be void?
  return true;
}

}  // namespace devtools_goma
