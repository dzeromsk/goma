// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "include_cache.h"

#include "absl/memory/memory.h"
#include "compiler_specific.h"
#include "content.h"
#include "counterz.h"
#include "cpp_directive_optimizer.h"
#include "cpp_directive_parser.h"
#include "cpp_parser.h"
#include "directive_filter.h"
#include "file_stat.h"
#include "goma_hash.h"
#include "histogram.h"
#include "include_guard_detector.h"

MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_stats.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

// IncludeCache::Item owns |content|.
class IncludeCache::Item {
 public:
  Item(std::unique_ptr<Content> content,
       IncludeItem include_item,
       absl::optional<SHA256HashValue> directive_hash,
       const FileStat& content_file_stat,
       size_t original_content_size)
      : content_(std::move(content)),
        include_item_(std::move(include_item)),
        directive_hash_(std::move(directive_hash)),
        original_content_size_(original_content_size),
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

    size_t original_content_size = content->size();
    std::unique_ptr<Content> filtered_content(
        DirectiveFilter::MakeFilteredContent(*content));

    CppDirectiveParser parser;
    CppDirectiveList directives;
    if (!parser.Parse(*filtered_content, &directives)) {
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
        std::move(filtered_content),
        IncludeItem(std::make_shared<CppDirectiveList>(std::move(directives)),
                    std::move(include_guard_ident)),
        directive_hash, file_stat, original_content_size);
  }

  const Content& content() const { return *content_; }
  const IncludeItem& include_item() const { return include_item_; }
  const absl::optional<SHA256HashValue>& directive_hash() const {
    return directive_hash_;
  }
  const FileStat& content_file_stat() const { return content_file_stat_; }

  size_t original_content_size() const { return original_content_size_; }

  size_t updated_count() const { return updated_count_; }
  void set_updated_count(size_t c) { updated_count_ = c; }

 private:
  const std::unique_ptr<Content> content_;
  const IncludeItem include_item_;
  const absl::optional<SHA256HashValue> directive_hash_;

  const size_t original_content_size_;

  const FileStat content_file_stat_;
  size_t updated_count_;

  DISALLOW_COPY_AND_ASSIGN(Item);
};

IncludeCache* IncludeCache::instance_;

// static
void IncludeCache::Init(int max_cache_size_in_mb,
                        bool calculates_directive_hash) {
  size_t max_cache_size = max_cache_size_in_mb * 1024LL * 1024LL;
  instance_ = new IncludeCache(max_cache_size, calculates_directive_hash);
  CppParser::EnsureInitialize();
}

// static
void IncludeCache::Quit() {
  delete instance_;
  instance_ = nullptr;
}

IncludeCache::IncludeCache(size_t max_cache_size,
                           bool calculates_directive_hash)
    : calculates_directive_hash_(calculates_directive_hash),
      count_item_updated_(0),
      count_item_evicted_(0),
      current_cache_size_(0),
      max_cache_size_(max_cache_size) {
}

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
  size_t filtered_content_size = item->content().size();

  auto it = cache_items_.find(key);
  if (it == cache_items_.end()) {
    cache_items_.emplace_back(key, std::move(item));
  } else {
    current_cache_size_ -= item->content().size();
    item->set_updated_count(it->second->updated_count() + 1);
    it->second = std::move(item);
  }

  current_cache_size_ += filtered_content_size;

  EvictCacheUnlocked();
}

void IncludeCache::EvictCacheUnlocked() {
  // Evicts older cache.
  while (max_cache_size_ < current_cache_size_) {
    DCHECK(!cache_items_.empty());

    current_cache_size_ -= cache_items_.front().second->content().size();
    cache_items_.pop_front();

    count_item_evicted_++;
  }
}

void IncludeCache::Dump(std::ostringstream* ss) {
  AUTO_SHARED_LOCK(lock, &rwlock_);

  size_t num_cache_item = cache_items_.size();

  Histogram compaction_ratio_histogram;
  compaction_ratio_histogram.SetName("Compaction Ratio Histogram [%]");

  Histogram item_update_count_histogram;
  item_update_count_histogram.SetName("Item Update Count Histogram");

  size_t total_original_size_in_bytes = 0;
  size_t total_filtered_size_in_bytes = 0;
  size_t max_original_size_in_bytes = 0;
  size_t max_filtered_size_in_bytes = 0;
  for (const auto& it : cache_items_) {
    const Item* item = it.second.get();
    total_original_size_in_bytes += item->original_content_size();
    max_original_size_in_bytes = std::max(max_original_size_in_bytes,
                                          item->original_content_size());

    total_filtered_size_in_bytes += item->content().size();
    max_filtered_size_in_bytes =
        std::max(max_filtered_size_in_bytes, item->content().size());

    double compaction_ratio = 0;
    if (item->original_content_size() > 0) {
      compaction_ratio = static_cast<double>(item->content().size()) /
                         item->original_content_size();
    }
    compaction_ratio_histogram.Add(compaction_ratio * 100);

    item_update_count_histogram.Add(item->updated_count());
  }

  (*ss) << "IncludeCaches summary" << std::endl;

  (*ss) << std::endl;
  (*ss) << "max cache size = "
        << max_cache_size_ << " bytes" << std::endl;
  (*ss) << "current cache size = "
        << current_cache_size_ << " bytes" << std::endl;

  (*ss) << std::endl;
  (*ss) << " Hit    = " << hit_count_.value() << std::endl;
  (*ss) << " Missed = " << missed_count_.value() << std::endl;

  (*ss) << std::endl;
  (*ss) << "Header num = " << num_cache_item << std::endl;

  if (num_cache_item > 0) {
    (*ss) << std::endl;

    (*ss) << "Original Headers: " << std::endl;
    (*ss) << "  Total   size = "
          << total_original_size_in_bytes << " bytes" << std::endl;
    (*ss) << "  Max     size = "
          << max_original_size_in_bytes << " bytes" << std::endl;
    (*ss) << "  Average size = "
          << (total_original_size_in_bytes / num_cache_item)
          << " bytes" << std::endl;

    (*ss) << "Filtered Headers: " << std::endl;
    (*ss) << "  Total   size = "
          << total_filtered_size_in_bytes << " bytes" << std::endl;
    (*ss) << "  Max     size = "
          << max_filtered_size_in_bytes << " bytes" << std::endl;
    (*ss) << "  Average size = "
          << (total_filtered_size_in_bytes / num_cache_item)
          << " bytes" << std::endl;

    (*ss) << std::endl;
    (*ss) << compaction_ratio_histogram.DebugString() << std::endl;

    (*ss) << std::endl;
    (*ss) << "Item updated count = " << count_item_updated_ << std::endl;
    (*ss) << "Item evicted count = " << count_item_evicted_ << std::endl;

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
    stats->set_total_cache_size(current_cache_size_);

    stats->set_updated(count_item_updated_);
    stats->set_evicted(count_item_evicted_);

    size_t total_original_size_in_bytes = 0;
    size_t total_filtered_size_in_bytes = 0;
    size_t max_original_size_in_bytes = 0;
    size_t max_filtered_size_in_bytes = 0;
    for (const auto& entry : cache_items_) {
      const Item* item = entry.second.get();

      total_original_size_in_bytes += item->original_content_size();
      max_original_size_in_bytes = std::max(max_original_size_in_bytes,
                                            item->original_content_size());

      total_filtered_size_in_bytes += item->content().size();
      max_filtered_size_in_bytes =
          std::max(max_filtered_size_in_bytes, item->content().size());
    }

    stats->set_original_total_size(total_original_size_in_bytes);
    stats->set_original_max_size(max_original_size_in_bytes);
    stats->set_filtered_total_size(total_filtered_size_in_bytes);
    stats->set_filtered_max_size(max_filtered_size_in_bytes);
  }
}

}  // namespace devtools_goma
