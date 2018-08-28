// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "include_cache.h"

#include "absl/memory/memory.h"
#include "compiler_specific.h"
#include "content.h"
#include "counterz.h"
#include "cxx/include_processor/cpp_directive_optimizer.h"
#include "cxx/include_processor/cpp_directive_parser.h"
#include "cxx/include_processor/directive_filter.h"
#include "cxx/include_processor/include_guard_detector.h"
#include "file_stat.h"
#include "goma_hash.h"
#include "histogram.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_stats.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

// IncludeCache::Item owns |content|.
class IncludeCache::Item {
 public:
  Item(IncludeItem include_item,
       absl::optional<SHA256HashValue> directive_hash,
       const FileStat& content_file_stat)
      : include_item_(std::move(include_item)),
        directive_hash_(std::move(directive_hash)),
        content_file_stat_(content_file_stat),
        updated_count_(0) {}

  ~Item() {}

  static std::unique_ptr<Item> CreateFromFile(const string& filepath,
                                              const FileStat& file_stat,
                                              bool needs_directive_hash) {
    std::unique_ptr<Content> content(Content::CreateFromFile(filepath));
    if (!content) {
      return nullptr;
    }

    std::unique_ptr<Content> filtered_content(
        DirectiveFilter::MakeFilteredContent(*content));

    CppDirectiveParser parser;
    CppDirectiveList directives;
    if (!parser.Parse(*filtered_content, filepath, &directives)) {
      return nullptr;
    }

    CppDirectiveOptimizer::Optimize(&directives);

    string include_guard_ident = IncludeGuardDetector::Detect(directives);

    absl::optional<SHA256HashValue> directive_hash;
    if (needs_directive_hash) {
      SHA256HashValue h;
      ComputeDataHashKeyForSHA256HashValue(filtered_content->ToStringView(),
                                           &h);
      directive_hash = std::move(h);
    }

    return absl::make_unique<Item>(
        IncludeItem(std::make_shared<CppDirectiveList>(std::move(directives)),
                    std::move(include_guard_ident)),
        directive_hash, file_stat);
  }

  const IncludeItem& include_item() const { return include_item_; }
  const absl::optional<SHA256HashValue>& directive_hash() const {
    return directive_hash_;
  }
  const FileStat& content_file_stat() const { return content_file_stat_; }

  size_t updated_count() const { return updated_count_; }
  void set_updated_count(size_t c) { updated_count_ = c; }

 private:
  const IncludeItem include_item_;
  const absl::optional<SHA256HashValue> directive_hash_;

  const FileStat content_file_stat_;
  size_t updated_count_;

  DISALLOW_COPY_AND_ASSIGN(Item);
};

IncludeCache* IncludeCache::instance_;

// static
void IncludeCache::Init(int max_cache_entries, bool calculates_directive_hash) {
  instance_ = new IncludeCache(max_cache_entries, calculates_directive_hash);
}

// static
void IncludeCache::Quit() {
  delete instance_;
  instance_ = nullptr;
}

IncludeCache::IncludeCache(size_t max_cache_entries,
                           bool calculates_directive_hash)
    : max_cache_entries_(max_cache_entries),
      calculates_directive_hash_(calculates_directive_hash),
      count_item_updated_(0),
      count_item_evicted_(0) {}

IncludeCache::~IncludeCache() {
}

IncludeItem IncludeCache::GetIncludeItem(const string& filepath,
                                         const FileStat& file_stat) {
  GOMA_COUNTERZ("GetDirectiveList");

  {
    AUTO_SHARED_LOCK(lock, &rwlock_);
    if (const Item* item = GetItemIfNotModifiedUnlocked(filepath, file_stat)) {
      hit_count_.Add(1);
      return item->include_item();
    }
  }

  missed_count_.Add(1);

  std::unique_ptr<Item> item(
      Item::CreateFromFile(filepath, file_stat, calculates_directive_hash()));
  if (!item) {
    return IncludeItem();
  }

  IncludeItem include_item = item->include_item();

  {
    AUTO_EXCLUSIVE_LOCK(lock, &rwlock_);
    InsertUnlocked(filepath, std::move(item), file_stat);
  }

  return include_item;
}

absl::optional<SHA256HashValue> IncludeCache::GetDirectiveHash(
    const string& filepath,
    const FileStat& file_stat) {
  DCHECK(calculates_directive_hash_);

  {
    AUTO_SHARED_LOCK(lock, &rwlock_);
    if (const Item* item = GetItemIfNotModifiedUnlocked(filepath, file_stat)) {
      return item->directive_hash();
    }
  }

  std::unique_ptr<Item> item(
      Item::CreateFromFile(filepath, file_stat, calculates_directive_hash()));
  if (!item) {
    return absl::nullopt;
  }
  absl::optional<SHA256HashValue> directive_hash = item->directive_hash();

  {
    AUTO_EXCLUSIVE_LOCK(lock, &rwlock_);
    InsertUnlocked(filepath, std::move(item), file_stat);
  }
  return directive_hash;
}

const IncludeCache::Item* IncludeCache::GetItemIfNotModifiedUnlocked(
    const string& key,
    const FileStat& file_stat) const {
  auto it = cache_items_.find(key);
  if (it == cache_items_.end())
    return nullptr;

  const Item* item = it->second.get();
  if (file_stat != item->content_file_stat())
    return nullptr;

  return item;
}

void IncludeCache::InsertUnlocked(const string& key,
                                  std::unique_ptr<Item> item,
                                  const FileStat& file_stat) {

  auto it = cache_items_.find(key);
  if (it == cache_items_.end()) {
    cache_items_.emplace_back(key, std::move(item));
  } else {
    item->set_updated_count(it->second->updated_count() + 1);
    it->second = std::move(item);
  }

  EvictCacheUnlocked();
}

void IncludeCache::EvictCacheUnlocked() {
  // Evicts older cache.
  while (max_cache_entries_ < cache_items_.size()) {
    DCHECK(!cache_items_.empty());
    cache_items_.pop_front();
    count_item_evicted_++;
  }
}

void IncludeCache::Dump(std::ostringstream* ss) {
  AUTO_SHARED_LOCK(lock, &rwlock_);

  size_t num_cache_item = cache_items_.size();

  Histogram item_update_count_histogram;
  item_update_count_histogram.SetName("Item Update Count Histogram");

  for (const auto& it : cache_items_) {
    const Item* item = it.second.get();
    item_update_count_histogram.Add(item->updated_count());
  }

  (*ss) << "IncludeCache summary" << std::endl;

  (*ss) << std::endl;
  (*ss) << "current cache entries = " << num_cache_item << std::endl
        << "entry capacity = " << max_cache_entries_ << std::endl;

  (*ss) << std::endl;
  (*ss) << " Hit    = " << hit_count_.value() << std::endl;
  (*ss) << " Missed = " << missed_count_.value() << std::endl;

  (*ss) << std::endl;
  (*ss) << "Item updated count = " << count_item_updated_ << std::endl;
  (*ss) << "Item evicted count = " << count_item_evicted_ << std::endl;

  // TODO: DebugString() will crash when there is no item.
  // Add a unittest and fix it later.
  if (num_cache_item > 0) {
    (*ss) << std::endl;
    (*ss) << item_update_count_histogram.DebugString() << std::endl;
  }

  (*ss) << std::endl;
}

// static
void IncludeCache::DumpAll(std::ostringstream* ss) {
  if (!IncludeCache::IsEnabled()) {
    (*ss) << "IncludeCache is not enabled." << std::endl;
    (*ss) << "To enable it, set environment variable "
          << "GOMA_MAX_INCLUDE_CACHE_SIZE more than 0." << std::endl;
    return;
  }

  instance()->Dump(ss);
}

void IncludeCache::DumpStatsToProto(IncludeCacheStats* stats) {

  stats->set_hit(hit_count_.value());
  stats->set_missed(missed_count_.value());

  {
    AUTO_SHARED_LOCK(lock, &rwlock_);
    stats->set_total_entries(cache_items_.size());

    stats->set_updated(count_item_updated_);
    stats->set_evicted(count_item_evicted_);
  }
}

}  // namespace devtools_goma
