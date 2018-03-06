// Copyright 2013 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "include_cache.h"

#include "compiler_specific.h"
#include "content.h"
#include "counterz.h"
#include "directive_filter.h"
#include "file_id.h"
#include "goma_hash.h"
#include "histogram.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO()
#include "prototmp/goma_stats.pb.h"
MSVC_POP_WARNING()

namespace devtools_goma {

// IncludeCache::Item owns |content|.
class IncludeCache::Item {
 public:
  Item(std::unique_ptr<Content> content, const FileId& content_file_id,
       const SHA256HashValue& directive_hash,
       size_t original_content_size, size_t updated_count)
      : content_(std::move(content)),
        content_file_id_(content_file_id),
        directive_hash_(directive_hash),
        original_content_size_(original_content_size),
        updated_count_(updated_count) {
  }

  ~Item() {}

  const Content* content() const {
    return content_.get();
  }

  const FileId& content_file_id() const {
    return content_file_id_;
  }

  const SHA256HashValue& directive_hash() const { return directive_hash_; }
  void set_directive_hash(const SHA256HashValue& hash) {
    directive_hash_ = hash;
  }

  size_t original_content_size() const {
    return original_content_size_;
  }

  size_t updated_count() const {
    return updated_count_;
  }

 private:
  const std::unique_ptr<Content> content_;
  const FileId content_file_id_;
  SHA256HashValue directive_hash_;
  const size_t original_content_size_;
  const size_t updated_count_;

  DISALLOW_COPY_AND_ASSIGN(Item);
};

IncludeCache* IncludeCache::instance_;

// static
void IncludeCache::Init(int max_cache_size_in_mb,
                        bool calculates_directive_hash) {
  if (max_cache_size_in_mb == 0)
    return;

  size_t max_cache_size = max_cache_size_in_mb * 1024LL * 1024LL;
  instance_ = new IncludeCache(max_cache_size, calculates_directive_hash);
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

const IncludeCache::Item* IncludeCache::GetItemIfNotModifiedUnlocked(
    const string& key, const FileId& file_id) const {
  auto it = cache_items_.find(key);
  if (it == cache_items_.end())
    return nullptr;

  const Item* item = it->second.get();
  if (file_id != item->content_file_id())
    return nullptr;

  return item;
}

std::unique_ptr<Content> IncludeCache::GetCopyIfNotModified(
    const string& filepath, const FileId& file_id) {
  GOMA_COUNTERZ("GetCopyIfNotModified");

  std::unique_ptr<Content> result;
  {
    // Since CreateFromContent might be heavy, we don't want to take
    // exclusive lock here.
    AUTO_SHARED_LOCK(lock, &rwlock_);
    const Item* item = GetItemIfNotModifiedUnlocked(filepath, file_id);
    if (item != nullptr) {
      GOMA_COUNTERZ("CreateFromContent");
      result = Content::CreateFromContent(*item->content());
    }
  }

  if (result != nullptr) {
    hit_count_.Add(1);
  } else {
    missed_count_.Add(1);
  }

  return result;
}

OptionalSHA256HashValue IncludeCache::GetDirectiveHash(const string& filepath,
                                                       const FileId& file_id) {
  DCHECK(calculates_directive_hash_);

  {
    AUTO_SHARED_LOCK(lock, &rwlock_);
    const Item* item = GetItemIfNotModifiedUnlocked(filepath, file_id);
    if (item != nullptr) {
      return OptionalSHA256HashValue(item->directive_hash());
    }
  }

  std::unique_ptr<Content> content(Content::CreateFromFile(filepath));
  if (content.get() == nullptr) {
    return OptionalSHA256HashValue();
  }

  SHA256HashValue hash_value;
  InsertInternal(filepath, *content, file_id, &hash_value);
  return OptionalSHA256HashValue(hash_value);
}

std::unique_ptr<Content> IncludeCache::Insert(
    const string& key, const Content& content, const FileId& content_file_id) {
  SHA256HashValue hash_value;
  return InsertInternal(key, content, content_file_id, &hash_value);
}

std::unique_ptr<Content> IncludeCache::InsertInternal(
    const string& key, const Content& content, const FileId& content_file_id,
    SHA256HashValue* directive_hash) {
  std::unique_ptr<Content> filtered_content =
      DirectiveFilter::MakeFilteredContent(content);
  std::unique_ptr<Content> returned_content(
      Content::CreateFromContent(*filtered_content));
  size_t original_size = content.size();

  if (calculates_directive_hash_) {
    DCHECK(directive_hash);
    ComputeDataHashKeyForSHA256HashValue(filtered_content->ToStringView(),
                                         directive_hash);
  }

  AUTO_EXCLUSIVE_LOCK(lock, &rwlock_);
  auto it = cache_items_.find(key);

  const size_t filtered_content_size = filtered_content->size();
  if (it == cache_items_.end()) {
    std::unique_ptr<Item> item(
        new Item(std::move(filtered_content), content_file_id,
                 *directive_hash, original_size, 0));
    cache_items_.emplace_back(key, std::move(item));
  } else {
    size_t original_updated_count = it->second->updated_count();
    ++count_item_updated_;
    current_cache_size_ -= it->second->content()->size();
    it->second.reset(new Item(std::move(filtered_content), content_file_id,
                              *directive_hash,
                              original_size, original_updated_count + 1));
  }

  current_cache_size_ += filtered_content_size;

  // Evicts older cache.
  CHECK_GT(max_cache_size_, 0U);
  while (max_cache_size_ < current_cache_size_) {
    DCHECK(!cache_items_.empty());

    current_cache_size_ -= cache_items_.front().second->content()->size();
    cache_items_.pop_front();

    count_item_evicted_++;
  }

  return returned_content;
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

    total_filtered_size_in_bytes += item->content()->size();
    max_filtered_size_in_bytes = std::max(max_filtered_size_in_bytes,
                                          item->content()->size());

    double compaction_ratio = 0;
    if (item->original_content_size() > 0) {
      compaction_ratio =
          static_cast<double>(item->content()->size()) /
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

      total_filtered_size_in_bytes += item->content()->size();
      max_filtered_size_in_bytes = std::max(max_filtered_size_in_bytes,
                                            item->content()->size());
    }

    stats->set_original_total_size(total_original_size_in_bytes);
    stats->set_original_max_size(max_original_size_in_bytes);
    stats->set_filtered_total_size(total_filtered_size_in_bytes);
    stats->set_filtered_max_size(max_filtered_size_in_bytes);
  }
}

}  // namespace devtools_goma
