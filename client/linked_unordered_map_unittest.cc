// Copyright 2017 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "linked_unordered_map.h"

#include <memory>
#include <string>
#include <vector>

#include <glog/stl_logging.h>
#include <gtest/gtest.h>

#include "goma_hash.h"
#include "sha256hash_hasher.h"

namespace devtools_goma {

namespace {
template<typename K, typename V, typename H>
std::vector<K> ListKeys(const LinkedUnorderedMap<K, V, H>& m) {
  std::vector<K> keys;
  for (const auto& entry : m) {
    keys.push_back(entry.first);
  }

  return keys;
}
}  // anonymous namespace

TEST(LinkedUnorderedMap, Empty) {
  LinkedUnorderedMap<int, int> m;

  EXPECT_EQ(m.size(), 0U);
  EXPECT_TRUE(m.empty());
}

TEST(LinkedUnorderedMap, Basic) {
  LinkedUnorderedMap<int, int> m;

  m.emplace_back(1, 100);
  m.emplace_back(4, 400);
  m.emplace_back(2, 200);
  m.emplace_back(3, 300);
  m.emplace_back(5, 500);

  EXPECT_EQ(m.size(), 5U);
  EXPECT_FALSE(m.empty());

  EXPECT_EQ(m.find(1)->second, 100);
  EXPECT_EQ(m.find(2)->second, 200);
  EXPECT_EQ(m.find(3)->second, 300);
  EXPECT_EQ(m.find(4)->second, 400);
  EXPECT_EQ(m.find(5)->second, 500);

  // insertion order must be preserved.
  EXPECT_EQ((std::vector<int> { 1, 4, 2, 3, 5 }), ListKeys(m));

  m.emplace_back(1, 1000);  // should override the previous '1'.
  EXPECT_EQ((std::vector<int> { 4, 2, 3, 5, 1 }), ListKeys(m));

  m.pop_front();
  EXPECT_EQ(m.size(), 4U);
  EXPECT_FALSE(m.empty());
  EXPECT_EQ((std::vector<int> { 2, 3, 5, 1 }), ListKeys(m));
  EXPECT_EQ(2, m.front().first);
  EXPECT_EQ(200, m.front().second);
}

TEST(LinkedUnorderedMap, NonCopyableType) {
  LinkedUnorderedMap<int, std::unique_ptr<int>> m;
  m.emplace_back(1, std::unique_ptr<int>(new int(100)));
  m.emplace_back(2, std::unique_ptr<int>(new int(200)));

  EXPECT_EQ(m.size(), 2U);
  EXPECT_EQ(100, *m.find(1)->second);
  EXPECT_EQ(200, *m.find(2)->second);

  m.pop_front();

  EXPECT_EQ(m.size(), 1U);
  EXPECT_TRUE(m.find(1) == m.end());
  EXPECT_EQ(200, *m.find(2)->second);
}

TEST(LinkedUnorderedMap, MoveToBack) {
  // Intentionally use move-only type in value to prove it works.
  LinkedUnorderedMap<int, std::unique_ptr<int>> m;
  m.emplace_back(1, std::unique_ptr<int>(new int(100)));
  m.emplace_back(2, std::unique_ptr<int>(new int(200)));
  m.emplace_back(3, std::unique_ptr<int>(new int(300)));

  {
    auto it = m.find(2);
    m.MoveToBack(it);

    EXPECT_EQ((std::vector<int> { 1, 3, 2 }), ListKeys(m));
    // `it` should be alive even if moved.
    EXPECT_EQ(200, *it->second);
    EXPECT_EQ(200, *m.find(2)->second);
  }

  {
    auto it = m.find(1);
    auto jt = m.find(3);

    m.MoveToBack(jt);
    EXPECT_EQ((std::vector<int> { 1, 2, 3 }), ListKeys(m));
    m.MoveToBack(it);
    EXPECT_EQ((std::vector<int> { 2, 3, 1 }), ListKeys(m));

    // still find-able.
    EXPECT_EQ(100, *m.find(1)->second);
    EXPECT_EQ(300, *m.find(3)->second);

    // |it| and |jt| should be alive.
    EXPECT_EQ(100, *it->second);
    EXPECT_EQ(300, *jt->second);
  }
}

TEST(LinkedUnorderedMap, CustomHashFunction) {
  LinkedUnorderedMap<SHA256HashValue, std::string, SHA256HashValueHasher> m;

  SHA256HashValue h1, h2;
  ASSERT_TRUE(SHA256HashValue::ConvertFromHexString(
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", &h1));
  ASSERT_TRUE(SHA256HashValue::ConvertFromHexString(
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", &h2));

  m.emplace_back(h1, "h1");
  m.emplace_back(h2, "h2");

  EXPECT_EQ(m.find(h1)->second, "h1");
  EXPECT_EQ(m.find(h2)->second, "h2");
}

}  // namespace devtools_goma
