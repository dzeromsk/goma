// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "include_cache.h"

#include <algorithm>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "content.h"
#include "file_id.h"
#include "file_id_cache.h"
#include "goma_hash.h"
#include "unittest_util.h"

using std::string;

namespace devtools_goma {

class IncludeCacheTest : public testing::Test {
 protected:
  void SetUp() override {
    IncludeCache::Init(1, true);  // 1MB
  }

  void TearDown() override {
    IncludeCache::Quit();
  }

  std::unique_ptr<Content> MakeDirectiveOnlyContent(int size) {
    string buf(size, '#');
    for (int i = 1023; i < size; i += 1024) {
      buf[i] = '\n';
    }

    return Content::CreateFromString(buf);
  }

  int Size(IncludeCache* include_cache) const {
    return include_cache->cache_items_.size();
  }

  size_t CacheSize(IncludeCache* include_cache) const {
    return include_cache->current_cache_size_;
  }
};

TEST_F(IncludeCacheTest, SetGet) {
  IncludeCache* ic = IncludeCache::instance();

  std::unique_ptr<Content> original(Content::CreateFromString(
      "#include <stdio.h>\n"
      "non-directive-line\n"));

  FileId file_id;
  file_id.size = original->size();
  file_id.mtime = 100;

  ic->Insert("kotori", *original, file_id);

  {
    std::unique_ptr<Content> content(
        ic->GetCopyIfNotModified("kotori", file_id));
    EXPECT_TRUE(content.get() != nullptr);

    string actual(content->buf(), content->buf_end());
    EXPECT_EQ("#include <stdio.h>\n", actual);
  }

  // When mtime is newer, we cannot take Content.
  file_id.mtime = 105;
  {
    std::unique_ptr<Content> content(
        ic->GetCopyIfNotModified("kotori", file_id));
    EXPECT_TRUE(content.get() == nullptr);
  }
}

TEST_F(IncludeCacheTest, ExceedMemory) {
  const int kFileSize = 256 * 1024;

  IncludeCache* ic = IncludeCache::instance();

  std::unique_ptr<Content> content(MakeDirectiveOnlyContent(kFileSize));

  FileId file_id;
  file_id.size = kFileSize;
  file_id.mtime = 100;

  ic->Insert("key0", *content, file_id);
  ic->Insert("key1", *content, file_id);
  ic->Insert("key2", *content, file_id);
  ic->Insert("key3", *content, file_id);

  EXPECT_EQ(4, Size(ic));
  EXPECT_EQ(1024 * 1024UL, CacheSize(ic));

  ic->Insert("key5", *content, file_id);

  // key0 has been evicted, since it is inserted first.
  EXPECT_EQ(4, Size(ic));
  EXPECT_EQ(1024 * 1024UL, CacheSize(ic));
  EXPECT_EQ(nullptr, ic->GetCopyIfNotModified("key0", file_id));

  // key1 is not evicted yet.
  std::unique_ptr<Content> key1_content(
      ic->GetCopyIfNotModified("key1", file_id));
  EXPECT_NE(nullptr, key1_content.get());

  // Insert key0 again.
  ic->Insert("key0", *content, file_id);

  // Then, key1 should be evicted.
  EXPECT_EQ(nullptr, ic->GetCopyIfNotModified("key1", file_id));
}

TEST_F(IncludeCacheTest, GetDirectiveHash)
{
  IncludeCache* ic = IncludeCache::instance();

  TmpdirUtil tmpdir("includecache");
  const string& ah = tmpdir.FullPath("a.h");
  const string& bh = tmpdir.FullPath("b.h");
  tmpdir.CreateTmpFile("a.h",
                       "#include <stdio.h>\n");
  tmpdir.CreateTmpFile("b.h",
                       "#include <math.h>\n");

  {
    SHA256HashValue hash_expected;
    ComputeDataHashKeyForSHA256HashValue("#include <stdio.h>\n",
                                         &hash_expected);

    FileIdCache file_id_cache;
    FileId file_id(file_id_cache.Get(ah));
    ASSERT_TRUE(file_id.IsValid());

    OptionalSHA256HashValue hash_actual = ic->GetDirectiveHash(ah, file_id);
    EXPECT_TRUE(hash_actual.valid());
    EXPECT_EQ(hash_expected, hash_actual.value());
  }

  // Update file content
  tmpdir.CreateTmpFile("a.h",
                       "#include <string.h>\n");
  {
    SHA256HashValue hash_expected;
    ComputeDataHashKeyForSHA256HashValue("#include <string.h>\n",
                                         &hash_expected);

    FileIdCache file_id_cache;
    FileId file_id(file_id_cache.Get(ah));
    ASSERT_TRUE(file_id.IsValid());

    OptionalSHA256HashValue hash_actual = ic->GetDirectiveHash(ah, file_id);
    EXPECT_TRUE(hash_actual.valid());
    EXPECT_EQ(hash_expected, hash_actual.value());
  }

  // Currently IncludeCache does not have a cache for b.h.
  // However, GetDirectiveHash will succeed, and the cached result
  // will be stored.
  {
    SHA256HashValue hash_expected;
    ComputeDataHashKeyForSHA256HashValue("#include <math.h>\n", &hash_expected);

    FileIdCache file_id_cache;
    FileId file_id(file_id_cache.Get(bh));
    ASSERT_TRUE(file_id.IsValid());

    OptionalSHA256HashValue hash_actual = ic->GetDirectiveHash(bh, file_id);
    EXPECT_TRUE(hash_actual.valid());
    EXPECT_EQ(hash_expected, hash_actual.value());

    std::unique_ptr<Content> content(ic->GetCopyIfNotModified(bh, file_id));
    ASSERT_TRUE(content.get() != nullptr);
    EXPECT_EQ("#include <math.h>\n", content->ToStringView());
  }
}

}  // namespace devtools_goma
