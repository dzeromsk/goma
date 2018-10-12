// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVTOOLS_GOMA_CLIENT_LINKED_UNORDERED_MAP_H_
#define DEVTOOLS_GOMA_CLIENT_LINKED_UNORDERED_MAP_H_

#include <list>

#include "absl/container/node_hash_map.h"

#include "glog/logging.h"

namespace devtools_goma {

// LinkedUnorderedMap is an unordered map, which keeps insertion order.
//
// Note: Unfortunately, this implementation prohibits from using move-only type
// in key, since key will be copied to list and map.
//
// This is not thread-safe.
template <typename K, typename V>
class LinkedUnorderedMap {
 public:
  using ListType = std::list<std::pair<K, V>>;
  using MapType = absl::node_hash_map<K, typename ListType::iterator>;
  using const_iterator = typename ListType::const_iterator;
  using iterator = typename ListType::iterator;

  size_t size() const {
    DCHECK_EQ(map_.size(), list_.size());
    return list_.size();
  }
  bool empty() const { return list_.empty(); }

  const std::pair<K, V>& front() const {
    DCHECK(!empty());
    return list_.front();
  }

  void pop_front();
  // This overwrites the previous entry if key is registered.
  // Note: To use universal reference, this method is template.
  // TODO: Aligh interface with std container.
  template<typename KK, typename VV>
  void emplace_back(KK&& k, VV&& v);

  // Move the value which iterator points to the last.
  void MoveToBack(iterator it);

  iterator begin() { return list_.begin(); }
  const_iterator begin() const { return list_.begin(); }
  iterator end() { return list_.end(); }
  const_iterator end() const { return list_.end(); }

  iterator find(const K& key);
  const_iterator find(const K& key) const;

 private:
  // Implementation Note: std::list iterator does not die after inserting
  // or deleting an entry. So, it is safe to have an list iterator in |map_|.
  ListType list_;
  MapType map_;
};

template <typename K, typename V>
void LinkedUnorderedMap<K, V>::pop_front() {
  DCHECK(!empty());
  map_.erase(list_.front().first);
  list_.pop_front();
}

template <typename K, typename V>
template <typename KK, typename VV>
void LinkedUnorderedMap<K, V>::emplace_back(KK&& key, VV&& value) {
  auto map_it = map_.find(key);
  if (map_it == map_.end()) {
    // key cannot be moved here. used later.
    list_.emplace_back(key, std::forward<VV>(value));
    auto back_it = list_.end();
    --back_it;
    map_.insert(make_pair(std::forward<KK>(key), back_it));
  } else {
    list_.erase(map_it->second);
    list_.emplace_back(std::forward<KK>(key), std::forward<VV>(value));
    auto back_it = list_.end();
    --back_it;
    map_it->second = back_it;
  }
}

template <typename K, typename V>
void LinkedUnorderedMap<K, V>::MoveToBack(
    typename LinkedUnorderedMap<K, V>::iterator it) {
  list_.splice(list_.end(), list_, it);
}

template <typename K, typename V>
typename LinkedUnorderedMap<K, V>::iterator LinkedUnorderedMap<K, V>::find(
    const K& key) {
  auto it = map_.find(key);
  if (it == map_.end()) {
    return list_.end();
  }

  return it->second;
}

template <typename K, typename V>
typename LinkedUnorderedMap<K, V>::const_iterator
LinkedUnorderedMap<K, V>::find(const K& key) const {
  auto it = map_.find(key);
  if (it == map_.end()) {
    return list_.end();
  }

  return it->second;
}

}  // namespace devtools_goma

#endif  // DEVTOOLS_GOMA_CLIENT_LINKED_UNORDERED_MAP_H_
