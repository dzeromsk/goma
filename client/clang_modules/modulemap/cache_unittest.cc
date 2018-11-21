// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cache.h"

#include <set>
#include <string>

#include "autolock_timer.h"
#include "client/unittest_util.h"
#include "gtest/gtest.h"

using std::string;

namespace devtools_goma {
namespace modulemap {

class ModuleMapCacheTest : public testing::Test {
 public:
  ModuleMapCacheTest() {
    modulemap::Cache::Init(10);

    tmpdir_util_ = absl::make_unique<TmpdirUtil>("modulemap-cache-unittest");
  }

  ~ModuleMapCacheTest() { modulemap::Cache::Quit(); }

 protected:
  string CreateTmpFile(const string& content, const string& name) {
    tmpdir_util_->CreateTmpFile(name, content);
    return tmpdir_util_->FullPath(name);
  }

  // Change All FileStat in cache so that the current time is newer than
  // the cached time.
  void EnsureNowIsNotNewerThanCachedFileStat() {
    AUTO_EXCLUSIVE_LOCK(lock, &modulemap::Cache::instance()->mu_);

    for (auto& entry : modulemap::Cache::instance()->cache_) {
      for (auto& mf : entry.second) {
        mf.checked_at += absl::Seconds(2);
      }
    }
  }

  std::unique_ptr<TmpdirUtil> tmpdir_util_;
};

TEST_F(ModuleMapCacheTest, Basic) {
  CreateTmpFile(R"(
module foo {
  extern module bar "bar.modulemap"
})",
                "foo.modulemap");
  CreateTmpFile(R"(
module bar {
  header "a.h"
})",
                "bar.modulemap");

  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_miss());

  {
    std::set<string> include_files;
    FileStatCache file_stat_cache;

    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "foo.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
  }

  // No cache, so the previous call should be cache-miss.
  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_miss());

  EnsureNowIsNotNewerThanCachedFileStat();

  {
    std::set<string> include_files;
    FileStatCache file_stat_cache;

    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "foo.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
  }

  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_miss());

  EnsureNowIsNotNewerThanCachedFileStat();

  // Update bar.modulemap
  CreateTmpFile(R"(
module bar {
  header "ab.h"
})",
                "bar.modulemap");

  {
    std::set<string> include_files;
    FileStatCache file_stat_cache;

    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "foo.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
  }

  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(2U, modulemap::Cache::instance()->cache_miss());
}

TEST_F(ModuleMapCacheTest, Spill) {
  // Re-init with size 2.
  modulemap::Cache::Quit();
  modulemap::Cache::Init(2);

  CreateTmpFile(R"(
module foo {
  header "a.h"
})",
                "foo.modulemap");
  CreateTmpFile(R"(
module bar {
  header "a.h"
})",
                "bar.modulemap");
  CreateTmpFile(R"(
module baz {
  header "a.h"
})",
                "baz.modulemap");

  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_miss());
  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_evicted());

  // Process "foo" and "bar".
  {
    std::set<string> include_files;
    FileStatCache file_stat_cache;

    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "foo.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "bar.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
  }

  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(2U, modulemap::Cache::instance()->cache_miss());
  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_evicted());

  EnsureNowIsNotNewerThanCachedFileStat();
  // Process "baz".
  {
    std::set<string> include_files;
    FileStatCache file_stat_cache;

    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "baz.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
  }

  EXPECT_EQ(0U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(3U, modulemap::Cache::instance()->cache_miss());
  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_evicted());

  EnsureNowIsNotNewerThanCachedFileStat();
  // Process "bar" again. It's not evicted.
  {
    std::set<string> include_files;
    FileStatCache file_stat_cache;

    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "bar.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
  }

  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(3U, modulemap::Cache::instance()->cache_miss());
  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_evicted());

  EnsureNowIsNotNewerThanCachedFileStat();
  // Process "foo" again. It's evicted.
  {
    std::set<string> include_files;
    FileStatCache file_stat_cache;

    EXPECT_TRUE(modulemap::Cache::instance()->AddModuleMapFileAndDependents(
        "foo.modulemap", tmpdir_util_->realcwd(), &include_files,
        &file_stat_cache));
  }

  EXPECT_EQ(1U, modulemap::Cache::instance()->cache_hit());
  EXPECT_EQ(4U, modulemap::Cache::instance()->cache_miss());
  EXPECT_EQ(2U, modulemap::Cache::instance()->cache_evicted());
}

}  // namespace modulemap
}  // namespace devtools_goma
