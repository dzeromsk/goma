// Copyright 2016 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "local_output_cache.h"

#include <memory>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "content.h"
#include "file.h"
#include "path.h"
#include "string_piece.h"
#include "unittest_util.h"

#ifdef _WIN32
# include "posix_helper_win.h"
#endif

namespace devtools_goma {

//
// <tmpdir>/cache -- LocalOutputCache
//          build -- build directory
//

class LocalOutputCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tmpdir_.reset(new TmpdirUtil("localoutputcache-test"));
    tmpdir_->MkdirForPath("build", true);
    tmpdir_->MkdirForPath("cache", true);
  }

  void TearDown() override {
    LocalOutputCache::Quit();
  }

  void InitLocalOutputCache() {
    const std::int64_t max_cache_amount = 1000000;
    const std::int64_t threshold_cache_amount = 10000000;
    const size_t max_items = 1000;
    const size_t threshold_items = 1000;
    InitLocalOutputCacheWithParams(max_cache_amount,
                                   threshold_cache_amount,
                                   max_items,
                                   threshold_items);
  }

  void InitLocalOutputCacheWithParams(std::int64_t max_cache_amount,
                                      std::int64_t threshold_cache_amount,
                                      size_t max_items,
                                      size_t threshold_items) {
    LocalOutputCache::Init(tmpdir_->FullPath("cache"),
                           nullptr,
                           max_cache_amount,
                           threshold_cache_amount,
                           max_items,
                           threshold_items);
  }

  ExecReq MakeFakeExecReq() {
    ExecReq req;
    req.mutable_command_spec()->set_name("clang");
    req.mutable_command_spec()->set_version("4.2.1");
    req.mutable_command_spec()->set_target("x86_64-unknown-linux-gnu");
    req.set_cwd(tmpdir_->FullPath("build"));
    return req;
  }

  ExecReq MakeFakeExecReqWithArgs(const std::vector<std::string>& args) {
    ExecReq req = MakeFakeExecReq();
    for (const auto& arg : args) {
      req.add_arg(arg);
    }
    return req;
  }

  ExecResp MakeFakeExecResp() {
    ExecResp resp;
    resp.mutable_result()->set_exit_status(0);
    ExecResult_Output* output = resp.mutable_result()->add_output();
    output->set_filename("output.o");
    return resp;
  }

  std::string CacheFilePath(StringPiece key) {
    return LocalOutputCache::instance()->CacheFilePath(key);
  }

  bool ShouldInvokeGarbageCollection() {
    return LocalOutputCache::instance()->ShouldInvokeGarbageCollection();
  }

  void RunGarbageCollection(LocalOutputCache::GarbageCollectionStat* stat) {
    LocalOutputCache::instance()->RunGarbageCollection(stat);
  }

  std::unique_ptr<TmpdirUtil> tmpdir_;
};

TEST_F(LocalOutputCacheTest, Match) {
  InitLocalOutputCache();

  const std::string trace_id = "(test-match)";

  // 1. Make ExecReq and ExecResp for fake compile
  ExecReq req = MakeFakeExecReq();
  ExecResp resp = MakeFakeExecResp();

  // 2. Try to Save output.
  tmpdir_->CreateTmpFile("build/output.o", "(output)");
  std::string key = LocalOutputCache::MakeCacheKey(req);

  EXPECT_TRUE(LocalOutputCache::instance()->SaveOutput(
                  key, &req, &resp, trace_id));

  // 3. Clean build directory
  tmpdir_->RemoveTmpFile("build/output.o");

  // 4. Lookup
  ExecResp looked_up_resp;
  EXPECT_TRUE(LocalOutputCache::instance()->Lookup(key,
                                                   &looked_up_resp,
                                                   trace_id));

  // 5. Check ExecResp content
  EXPECT_EQ(1, looked_up_resp.result().output_size());
  EXPECT_EQ("output.o",
            looked_up_resp.result().output(0).filename());
}

TEST_F(LocalOutputCacheTest, NoMatch) {
  InitLocalOutputCache();

  const std::string trace_id = "(test-nomatch)";

  // 1. Make ExecReq and ExecResp for fake compile
  ExecReq req = MakeFakeExecReq();
  ExecResp resp = MakeFakeExecResp();

  // 2. Try to Save output.
  tmpdir_->CreateTmpFile("build/output.o", "(output)");
  std::string key = LocalOutputCache::MakeCacheKey(req);

  EXPECT_TRUE(LocalOutputCache::instance()->SaveOutput(
                  key, &req, &resp, trace_id));

  // 3. Clean build directory
  tmpdir_->RemoveTmpFile("build/output.o");

  // 4. Lookup (should fail here)
  ExecResp looked_up_resp;
  std::string fake_key =
      "000000000000000000000000000000000000000000000000000000000000fa6e";
  EXPECT_FALSE(LocalOutputCache::instance()->Lookup(fake_key,
                                                    &looked_up_resp,
                                                    trace_id));
}

TEST_F(LocalOutputCacheTest, CollectGarbage) {
  InitLocalOutputCacheWithParams(0, 0, 100, 100);

  const std::string trace_id = "(garbage)";

  // Make Item.
  ExecReq req = MakeFakeExecReq();
  ExecResp resp = MakeFakeExecResp();
  tmpdir_->CreateTmpFile("build/output.o", "(output)");
  std::string key = LocalOutputCache::instance()->MakeCacheKey(req);
  EXPECT_TRUE(LocalOutputCache::instance()->SaveOutput(
                  key, &req, &resp, trace_id));

  // Check key exists.
  std::string path = CacheFilePath(key);
  EXPECT_EQ(0, access(path.c_str(), F_OK));

  // The item should be removed here, since max cache amount is small enough.
  {
    LocalOutputCache::GarbageCollectionStat stat;
    RunGarbageCollection(&stat);
    EXPECT_NE(0, access(path.c_str(), F_OK));
    EXPECT_EQ(1U, stat.num_removed);
    EXPECT_EQ(0U, stat.num_failed);
  }
}

TEST_F(LocalOutputCacheTest, WontCollectGarbage) {
  InitLocalOutputCacheWithParams(1000000, 1000000, 100, 100);

  const std::string trace_id = "(garbage)";

  // Make Item.
  ExecReq req = MakeFakeExecReq();
  ExecResp resp = MakeFakeExecResp();
  tmpdir_->CreateTmpFile("build/output.o", "(output)");
  std::string key = LocalOutputCache::instance()->MakeCacheKey(req);
  EXPECT_TRUE(LocalOutputCache::instance()->SaveOutput(
                  key, &req, &resp, trace_id));

  // Check key exists.
  std::string path = CacheFilePath(key);
  EXPECT_EQ(0, access(path.c_str(), F_OK));

  // Run garbage collection. Here, anything won't be removed, since
  // max cache amount is large enough.
  {
    LocalOutputCache::GarbageCollectionStat stat;
    RunGarbageCollection(&stat);
    EXPECT_EQ(0, access(path.c_str(), F_OK));
    EXPECT_EQ(0U, stat.num_removed);
    EXPECT_EQ(0U, stat.num_failed);
  }
}

TEST_F(LocalOutputCacheTest, CollectGarbageByNumItems) {
  // Allow max 99 items.
  InitLocalOutputCacheWithParams(10000000, 10000000, 99, 60);

  const std::string trace_id = "(garbage)";

  std::vector<std::string> keys;
  std::unordered_set<std::string> key_set;

  // Make 99 items.
  for (int i = 0; i < 99; ++i) {
    ExecReq req = MakeFakeExecReqWithArgs(std::vector<std::string> {
        "clang",
        "-DFOO=" + std::to_string(i),
    });

    ExecResp resp = MakeFakeExecResp();
    tmpdir_->CreateTmpFile("build/output.o", "(output)");
    std::string key = LocalOutputCache::instance()->MakeCacheKey(req);
    keys.push_back(key);
    key_set.insert(key);
    EXPECT_TRUE(LocalOutputCache::instance()->SaveOutput(
                    key, &req, &resp, trace_id));
  }

  // All keys must be different.
  EXPECT_EQ(99UL, key_set.size());

  // Check key exists.
  for (const auto& key : keys) {
    std::string path = CacheFilePath(key);
    EXPECT_EQ(0, access(path.c_str(), F_OK));
  }

  // GC won't run yet.
  EXPECT_FALSE(ShouldInvokeGarbageCollection());

  // Add last one.
  {
    ExecReq req = MakeFakeExecReqWithArgs(std::vector<std::string> {
        "clang",
        "-DFOO=" + std::to_string(99),
    });

    ExecResp resp = MakeFakeExecResp();
    tmpdir_->CreateTmpFile("build/output.o", "(output)");
    std::string key = LocalOutputCache::instance()->MakeCacheKey(req);
    keys.push_back(key);
    key_set.insert(key);
    EXPECT_TRUE(LocalOutputCache::instance()->SaveOutput(
                    key, &req, &resp, trace_id));
  }

  // All keys must be different.
  EXPECT_EQ(100UL, key_set.size());

  // GC should run now.
  EXPECT_TRUE(ShouldInvokeGarbageCollection());

  // Run garbage collection.
  // Since threshold is 60, 40 items must be removed.
  {
    LocalOutputCache::GarbageCollectionStat stat;
    RunGarbageCollection(&stat);
    EXPECT_EQ(40U, stat.num_removed);
    EXPECT_EQ(0U, stat.num_failed);
  }
}

}  // namespace devtools_goma
