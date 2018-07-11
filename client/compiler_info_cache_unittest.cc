// Copyright 2015 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "compiler_info_cache.h"

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "absl/memory/memory.h"
#include "compiler_flags.h"
#include "compiler_flags_parser.h"
#include "compiler_info_builder_facade.h"
#include "compiler_info_state.h"
#include "path.h"
#include "subprocess.h"
#include "unittest_util.h"
#include "util.h"

using std::string;

namespace {
const int kCacheHoldingTimeSec = 60 * 60 * 24 * 30;  // 30 days
}

namespace devtools_goma {

class TestCompilerInfoValidator
    : public CompilerInfoCache::CompilerInfoValidator {
 public:
  TestCompilerInfoValidator() {}

  bool Validate(const CompilerInfo& compiler_info,
                const string& local_compiler_path) override {
    return true;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestCompilerInfoValidator);
};


class HashCheckingCompilerInfoValidator
    : public CompilerInfoCache::CompilerInfoValidator {
 public:
  HashCheckingCompilerInfoValidator() {}

  bool Validate(const CompilerInfo& compiler_info,
                const string& local_compiler_path) override {
    // If FileStat is the same, this should be ok.
    if (compiler_info.local_compiler_stat() == local_compiler_file_stat_) {
      return true;
    }

    // Otherwise, we check hash. If hash is the same, it's still ok.
    if (compiler_info.local_compiler_hash() == local_compiler_hash_) {
      return true;
    }

    // compiler is updated.
    return false;
  }

  void SetLocalCompilerHash(const std::string& hash) {
    local_compiler_hash_ = hash;
  }
  void SetLocalCompilerFileStat(const FileStat& file_stat) {
    local_compiler_file_stat_ = file_stat;
  }

 private:
  std::string local_compiler_hash_;
  FileStat local_compiler_file_stat_;
};

class CompilerInfoCacheTest : public testing::Test {
 public:
  CompilerInfoCacheTest()
      : cache_(new CompilerInfoCache("", kCacheHoldingTimeSec)),
        validator_(new TestCompilerInfoValidator) {
    cache_->SetValidator(validator_);
  }

 protected:
  bool Unmarshal(const CompilerInfoDataTable& table) {
    return cache_->Unmarshal(table);
  }
  bool Marshal(CompilerInfoDataTable* table) {
    return cache_->Marshal(table);
  }
  string HashKey(const CompilerInfoData& data) {
    return cache_->HashKey(data);
  }
  void Clear() {
    cache_->Clear();
  }
  void UpdateOlderCompilerInfo() {
    cache_->UpdateOlderCompilerInfo();
  }

  void SetFailedAt(CompilerInfoState* state, time_t failed_at) {
    // TODO: in prod code, CompilerInfo would never be updated like this.
    // error message has been changed only if new CompilerInfo data is stored.
    CompilerInfoBuilder::OverrideError(
        "error message by SetFailedAt()", failed_at,
        state->compiler_info_->data_.get());
  }

  void SetValidator(CompilerInfoCache::CompilerInfoValidator* validator) {
    cache_->SetValidator(validator);
    validator_ = validator;
  }

  void SetCompilerInfoFileStat(CompilerInfo* compiler_info,
                               const FileStat& file_stat) {
    compiler_info->local_compiler_stat_ = file_stat;
  }

  void SetCompilerInfoHash(CompilerInfo* compiler_info,
                           const std::string& hash) {
    compiler_info->data_->set_hash(hash);
  }

  const std::unordered_map<string, CompilerInfoState*>& compiler_info() const {
    return cache_->compiler_info_;
  }

  const std::unordered_map<string, std::unique_ptr<std::unordered_set<string>>>&
  keys_by_hash() const {
    return cache_->keys_by_hash_;
  }

  std::unique_ptr<CompilerInfoCache> cache_;
  CompilerInfoCache::CompilerInfoValidator* validator_;  // Owned by cache_.
};

TEST_F(CompilerInfoCacheTest, Lookup) {
  std::vector<string> args;
  args.push_back("/usr/bin/gcc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  std::vector<string> key_env;

  CompilerInfoCache::Key key(CompilerInfoCache::CreateKey(
      *flags, "/usr/bin/gcc", key_env));
  ScopedCompilerInfoState cis(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == nullptr);

  // get valid compiler info.
  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->set_found(true);
  cid->mutable_cxx();

  cis.reset(cache_->Store(key, std::move(cid)));
  EXPECT_EQ(2, cis.get()->refcnt());  // caller & in cache

  CompilerInfoState* state = cis.get();
  cis.reset(nullptr);
  EXPECT_EQ(1, state->refcnt());  // in cache

  // When taking the second compiler info, we don't need to fill
  // CompilerInfo again.
  cis.reset(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == state);
  EXPECT_EQ(2, cis.get()->refcnt()); // caller & in cache.

  ScopedCompilerInfoState cis2(std::move(cis));
  EXPECT_TRUE(cis.get() == nullptr);
  EXPECT_TRUE(cis2.get() == state);
  EXPECT_EQ(2, cis2.get()->refcnt());

  cis2.reset(nullptr);
  EXPECT_EQ(1, state->refcnt()); // in cache.
}

TEST_F(CompilerInfoCacheTest, CompilerInfoCacheKeyRelative) {
  std::vector<string> args {"./clang"};
  std::vector<string> key_env;

  std::unique_ptr<CompilerFlags> flags1(
      CompilerFlagsParser::MustNew(args, "/dir1"));
  std::unique_ptr<CompilerFlags> flags2(
      CompilerFlagsParser::MustNew(args, "/dir2"));

  CompilerInfoCache::Key key1(CompilerInfoCache::CreateKey(
      *flags1, "./clang", key_env));
  CompilerInfoCache::Key key2(CompilerInfoCache::CreateKey(
      *flags2, "./clang", key_env));

  EXPECT_FALSE(file::IsAbsolutePath(key1.local_compiler_path));
  EXPECT_FALSE(file::IsAbsolutePath(key2.local_compiler_path));

  EXPECT_NE(key1.ToString(CompilerInfoCache::Key::kCwdRelative),
            key2.ToString(CompilerInfoCache::Key::kCwdRelative));
}

TEST_F(CompilerInfoCacheTest, CompilerInfoCacheKeyAbsolute) {
  std::vector<string> args {"/usr/bin/clang"};
  std::vector<string> key_env;

  std::unique_ptr<CompilerFlags> flags1(
      CompilerFlagsParser::MustNew(args, "/dir1"));
  std::unique_ptr<CompilerFlags> flags2(
      CompilerFlagsParser::MustNew(args, "/dir2"));

  CompilerInfoCache::Key key1(CompilerInfoCache::CreateKey(
      *flags1, "/usr/bin/clang", key_env));
  CompilerInfoCache::Key key2(CompilerInfoCache::CreateKey(
      *flags2, "/usr/bin/clang", key_env));

  EXPECT_TRUE(file::IsAbsolutePath(key1.local_compiler_path));
  EXPECT_TRUE(file::IsAbsolutePath(key2.local_compiler_path));

  EXPECT_NE(key1.ToString(CompilerInfoCache::Key::kCwdRelative),
            key2.ToString(CompilerInfoCache::Key::kCwdRelative));

  EXPECT_EQ(key1.ToString(!CompilerInfoCache::Key::kCwdRelative),
            key2.ToString(!CompilerInfoCache::Key::kCwdRelative));
}

TEST_F(CompilerInfoCacheTest, DupStore) {
  std::vector<string> args;
  args.push_back("/usr/bin/gcc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  std::vector<string> key_env;

  CompilerInfoCache::Key key(CompilerInfoCache::CreateKey(
      *flags, "/usr/bin/gcc", key_env));
  ScopedCompilerInfoState cis(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == nullptr);

  time_t now = time(nullptr);
  // get valid compiler info.
  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->set_last_used_at(now);
  cid->set_found(true);
  cid->mutable_cxx();

  cis.reset(cache_->Store(key, std::move(cid)));
  EXPECT_EQ(2, cis.get()->refcnt());  // caller & in cache

  {
    EXPECT_EQ(1U, keys_by_hash().size());
    const std::unordered_set<string>& keys = *keys_by_hash().begin()->second;
    EXPECT_EQ(1U, keys.size());
  }

  CompilerInfoState* state = cis.get();
  cis.reset(nullptr);
  EXPECT_EQ(1, state->refcnt());  // in cache

  // When taking the second compiler info, we don't need to fill
  // CompilerInfo again.
  cis.reset(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == state);
  EXPECT_EQ(2, cis.get()->refcnt());  // caller & in cache.

  // different compiler_info_key;
  args.push_back("-fPIC");
  flags = CompilerFlagsParser::MustNew(args, "/tmp");

  CompilerInfoCache::Key key2(CompilerInfoCache::CreateKey(
      *flags, "/usr/bin/gcc", key_env));
  EXPECT_NE(key.base, key2.base);
  ASSERT_TRUE(file::IsAbsolutePath(key.local_compiler_path));
  ASSERT_TRUE(file::IsAbsolutePath(key2.local_compiler_path));
  EXPECT_NE(key.ToString(false),
            key2.ToString(false));

  cis.reset(cache_->Lookup(key2));
  EXPECT_TRUE(cis.get() == nullptr);

  // get valid compiler info, which is the same as before.
  cid = absl::make_unique<CompilerInfoData>();
  cid->set_last_used_at(now);
  cid->set_found(true);
  cid->mutable_cxx();

  cis.reset(cache_->Store(key2, std::move(cid)));
  EXPECT_EQ(3, cis.get()->refcnt());  // caller & in cache (for key and key2).
  EXPECT_TRUE(cis.get() == state);  // same as before.

  {
    EXPECT_EQ(1U, keys_by_hash().size());
    const std::unordered_set<string>& keys = *keys_by_hash().begin()->second;
    EXPECT_EQ(2U, keys.size());
  }

  // update with different data.
  cid = absl::make_unique<CompilerInfoData>();
  cid->set_last_used_at(now);
  cid->set_name("gcc");
  cid->set_found(true);
  cid->mutable_cxx();

  cis.reset(cache_->Store(key2, std::move(cid)));
  EXPECT_EQ(2, cis.get()->refcnt());  // caller & in cache (for key2).
  EXPECT_TRUE(cis.get() != state);  // different

  {
    EXPECT_EQ(2U, keys_by_hash().size());
    for (const auto& it : keys_by_hash()) {
      EXPECT_EQ(1U, it.second->size());
    }
  }

  cis.reset(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == state);
  EXPECT_EQ(2, cis.get()->refcnt());  // caller & in cache (for key).
}

TEST_F(CompilerInfoCacheTest, NegativeCache) {
  const string compiler_path("/invalid/gcc");

  std::vector<string> args;
  args.push_back(compiler_path);
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  std::vector<string> key_env;

  CompilerInfoCache::Key key(CompilerInfoCache::CreateKey(
      *flags, compiler_path, key_env));

  // Taking CompilerInfo should fail.
  ScopedCompilerInfoState cis(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == nullptr);

  // get error compiler info
  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->set_found(true);
  cid->mutable_cxx();
  CompilerInfoBuilder::AddErrorMessage("invalid gcc", cid.get());

  cis.reset(cache_->Store(key, std::move(cid)));
  EXPECT_EQ(1, cache_->NumFail());
  EXPECT_EQ(0, cache_->NumMiss());
  EXPECT_TRUE(cis.get() != nullptr);
  EXPECT_EQ(2, cis.get()->refcnt());  // caller & in cache
  EXPECT_TRUE(cis.get()->info().found());
  EXPECT_TRUE(cis.get()->info().HasError());
  EXPECT_GT(cis.get()->info().failed_at(), 0);

  // will get negatively cached CompilerInfo.
  ScopedCompilerInfoState cis2(cache_->Lookup(key));
  EXPECT_TRUE(cis2.get() == cis.get());
  EXPECT_EQ(3, cis.get()->refcnt());  // cis, cis2 & in cache
  EXPECT_EQ(1, cache_->NumFail());
  EXPECT_EQ(0, cache_->NumMiss());

  cis2.reset(nullptr);
  EXPECT_EQ(2, cis.get()->refcnt());  // cis & in cache

  // Sets old failed_at time.
  time_t now = time(nullptr);
  SetFailedAt(cis.get(), now - 3600);

  // Since the negative cache is expired, we will get no CompilerInfo,
  // and will need to retry to make CompilerInfo again.
  cis2.reset(cache_->Lookup(key));
  EXPECT_TRUE(cis2.get() == nullptr);
  EXPECT_EQ(2, cis.get()->refcnt()); // cis & in cache

  // get compiler info again, and update.
  std::unique_ptr<CompilerInfoData> cid2(new CompilerInfoData);
  cid2->set_found(true);
  cid2->mutable_cxx();
  CompilerInfoBuilder::AddErrorMessage("invalid gcc", cid2.get());

  cis2.reset(cache_->Store(key, std::move(cid2)));
  EXPECT_EQ(2, cis2.get()->refcnt());  // cis2 & in cache
  EXPECT_EQ(1, cis.get()->refcnt());  // cis only, removed from cache.
  EXPECT_EQ(2, cache_->NumFail());
  EXPECT_EQ(0, cache_->NumMiss());
}

TEST_F(CompilerInfoCacheTest, MissingCompilerCache) {
  const string compiler_path("/missing/gcc");

  std::vector<string> args;
  args.push_back(compiler_path);
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  std::vector<string> key_env;

  CompilerInfoCache::Key key(CompilerInfoCache::CreateKey(
      *flags, compiler_path, key_env));

  // Taking CompilerInfo should fail.
  ScopedCompilerInfoState cis(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == nullptr);

  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->mutable_cxx();
  CompilerInfoBuilder::AddErrorMessage("Couldn't open local compiler file",
                                       cid.get());
  cis.reset(cache_->Store(key, std::move(cid)));
  EXPECT_EQ(2, cis.get()->refcnt()); // caller & in cache
  EXPECT_EQ(0, cache_->NumFail());
  EXPECT_EQ(1, cache_->NumMiss());
  EXPECT_TRUE(cis.get()->info().HasError());
  EXPECT_FALSE(cis.get()->info().found());
  EXPECT_GT(cis.get()->info().failed_at(), 0);

  // will get negatively cached CompilerInfo.
  ScopedCompilerInfoState cis2(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == cis2.get());
  EXPECT_EQ(3, cis.get()->refcnt());  // cis, cis2 & in cache.
  EXPECT_EQ(0, cache_->NumFail());
  EXPECT_EQ(1, cache_->NumMiss());
  EXPECT_TRUE(cis2.get()->info().HasError());
  EXPECT_FALSE(cis2.get()->info().found());
  EXPECT_GT(cis2.get()->info().failed_at(), 0);

  cis2.reset(nullptr);
  EXPECT_EQ(2, cis.get()->refcnt());  // cis & in cache

  // Sets old failed_at time.
  time_t now = time(nullptr);
  SetFailedAt(cis.get(), now - 3600);

  // Since the negative cache is expired, we will retry to make
  // CompilerInfo again.
  cis2.reset(cache_->Lookup(key));
  EXPECT_TRUE(cis2.get() == nullptr);
  EXPECT_EQ(2, cis.get()->refcnt()); // cis & still in cache

  // get compiler info again, and update.
  std::unique_ptr<CompilerInfoData> cid2(new CompilerInfoData);
  cid2->mutable_cxx();
  CompilerInfoBuilder::AddErrorMessage("Couldn't open local compiler file",
                                       cid2.get());
  cis2.reset(cache_->Store(key, std::move(cid2)));
  EXPECT_EQ(2, cis2.get()->refcnt());  // cis2 & in cache
  EXPECT_EQ(1, cis.get()->refcnt());  // cis only, removed from cache.
  EXPECT_EQ(0, cache_->NumFail());
  EXPECT_EQ(2, cache_->NumMiss());
  EXPECT_TRUE(cis2.get()->info().HasError());
  EXPECT_FALSE(cis2.get()->info().found());
  EXPECT_GT(cis2.get()->info().failed_at(), 0);

}

TEST_F(CompilerInfoCacheTest, Marshal) {
  CompilerInfoCache::Key key;
  key.base = "/usr/bin/gcc -O2";
  key.cwd = "/b/build/slave/work";
  key.local_compiler_path = "/usr/bin/gcc";

  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->set_name("gcc");
  cid->set_lang("c");
  cid->set_found(true);
  cid->mutable_cxx();
  const string hash1 = HashKey(*cid.get());

  ASSERT_TRUE(file::IsAbsolutePath(key.local_compiler_path));
  const string key1 = key.ToString(
      !CompilerInfoCache::Key::kCwdRelative);
  ScopedCompilerInfoState cis(cache_->Store(key, std::move(cid)));

  key.base = "/usr/bin/gcc -O2 -fno-diagnostics-show-option";
  cid = absl::make_unique<CompilerInfoData>();
  cid->set_name("gcc");
  cid->set_lang("c");
  cid->set_found(true);
  cid->mutable_cxx();
  EXPECT_EQ(hash1, HashKey(*cid.get()));
  ASSERT_TRUE(file::IsAbsolutePath(key.local_compiler_path));
  const string key2 = key.ToString(
      !CompilerInfoCache::Key::kCwdRelative);
  EXPECT_NE(key1, key2);
  cis.reset(cache_->Store(key, std::move(cid)));

  key.base = "/usr/bin/g++ -O2";
  key.local_compiler_path = "/usr/bin/g++";
  cid = absl::make_unique<CompilerInfoData>();
  cid->set_name("g++");
  cid->set_lang("c++");
  cid->set_found(true);
  cid->mutable_cxx();
  const string hash3 = HashKey(*cid.get());
  EXPECT_NE(hash1, hash3);
  ASSERT_TRUE(file::IsAbsolutePath(key.local_compiler_path));
  const string key3 = key.ToString(
      !CompilerInfoCache::Key::kCwdRelative);
  EXPECT_NE(key1, key3);
  EXPECT_NE(key2, key3);
  cis.reset(cache_->Store(key, std::move(cid)));

  key.base = "/usr/bin/clang";
  key.local_compiler_path = "/usr/bin/clang";
  cid = absl::make_unique<CompilerInfoData>();
  cid->set_name("clang");
  cid->set_lang("c");
  cid->set_found(true);
  cid->mutable_cxx();
  const string hash4 = HashKey(*cid.get());
  EXPECT_NE(hash1, hash4);
  EXPECT_NE(hash3, hash4);
  ASSERT_TRUE(file::IsAbsolutePath(key.local_compiler_path));
  const string key4 = key.ToString(
      !CompilerInfoCache::Key::kCwdRelative);
  EXPECT_NE(key1, key4);
  EXPECT_NE(key2, key4);
  EXPECT_NE(key3, key4);
  cis.reset(cache_->Store(key, std::move(cid)));
  cis.get()->SetDisabled(true, "disabled for test");

  cis.reset(nullptr);

  CompilerInfoDataTable table;
  EXPECT_TRUE(Marshal(&table));

  EXPECT_EQ(2, table.compiler_info_data_size());
  bool hash1_found = false;
  bool hash3_found = false;
  for (int i = 0; i < 2; ++i) {
    const CompilerInfoDataTable::Entry& entry = table.compiler_info_data(i);
    switch (entry.keys_size()) {
      case 2: // hash1: key1, key2
        {
          std::unordered_set<string> keys(entry.keys().begin(),
                                          entry.keys().end());
          EXPECT_EQ(1U, keys.count(key1));
          EXPECT_EQ(1U, keys.count(key2));
          EXPECT_EQ("gcc", entry.data().name());
          EXPECT_EQ("c", entry.data().lang());
          EXPECT_TRUE(entry.data().found());
          EXPECT_EQ(hash1, HashKey(entry.data()));
          hash1_found = true;
        }
        break;
      case 1: // hash3: key3
        {
          EXPECT_EQ(key3, entry.keys(0));
          EXPECT_EQ("g++", entry.data().name());
          EXPECT_EQ("c++", entry.data().lang());
          EXPECT_TRUE(entry.data().found());
          EXPECT_EQ(hash3, HashKey(entry.data()));
          hash3_found = true;
        }
        break;
      default:
        ADD_FAILURE() << "unexpected entry[" << i << "].keys_size()"
                      << entry.keys_size();
    }
  }
  EXPECT_TRUE(hash1_found);
  EXPECT_TRUE(hash3_found);
}

TEST_F(CompilerInfoCacheTest, Unmarshal) {
  CompilerInfoDataTable table;
  CompilerInfoDataTable::Entry* entry = table.add_compiler_info_data();
  entry->add_keys("/usr/bin/gcc -O2 @");
  entry->add_keys("/usr/bin/gcc -O2 -fno-diagnostics-show-option @");
  CompilerInfoData* data = entry->mutable_data();
  data->set_name("gcc");
  data->set_lang("c");
  data->set_found(true);
  data->mutable_cxx();

  entry = table.add_compiler_info_data();
  entry->add_keys("/usr/bin/g++ -O2 @");
  data = entry->mutable_data();
  data->set_name("g++");
  data->set_lang("c++");
  data->set_found(true);
  data->mutable_cxx();

  EXPECT_TRUE(Unmarshal(table));

  EXPECT_EQ(3U, compiler_info().size());
  auto p = compiler_info().find("/usr/bin/gcc -O2 @");
  EXPECT_TRUE(p != compiler_info().end());
  CompilerInfoState* state = p->second;
  EXPECT_EQ(2, state->refcnt());
  EXPECT_EQ("gcc", state->info().data().name());
  EXPECT_EQ("c", state->info().data().lang());
  EXPECT_TRUE(state->info().data().found());
  const string& hash1 = HashKey(state->info().data());

  p = compiler_info().find("/usr/bin/gcc -O2 -fno-diagnostics-show-option @");
  EXPECT_TRUE(p != compiler_info().end());
  state = p->second;
  EXPECT_EQ(2, state->refcnt());
  EXPECT_EQ("gcc", state->info().data().name());
  EXPECT_EQ("c", state->info().data().lang());
  EXPECT_TRUE(state->info().data().found());
  EXPECT_EQ(hash1, HashKey(state->info().data()));

  p = compiler_info().find("/usr/bin/g++ -O2 @");
  EXPECT_TRUE(p != compiler_info().end());
  state = p->second;
  EXPECT_EQ(1, state->refcnt());
  EXPECT_EQ("g++", state->info().data().name());
  EXPECT_EQ("c++", state->info().data().lang());
  EXPECT_TRUE(state->info().data().found());
  const string& hash2 = HashKey(state->info().data());
  EXPECT_NE(hash1, hash2);

  EXPECT_EQ(2U, keys_by_hash().size());
  auto found = keys_by_hash().find(hash1);
  EXPECT_TRUE(found != keys_by_hash().end());
  const std::unordered_set<string>* keys = found->second.get();
  EXPECT_EQ(2U, keys->size());
  EXPECT_EQ(1U, keys->count("/usr/bin/gcc -O2 @"));
  EXPECT_EQ(1U, keys->count("/usr/bin/gcc -O2 -fno-diagnostics-show-option @"));

  found = keys_by_hash().find(hash2);
  EXPECT_TRUE(found != keys_by_hash().end());
  keys = found->second.get();
  EXPECT_EQ(1U, keys->size());
  EXPECT_EQ(1U, keys->count("/usr/bin/g++ -O2 @"));
}

TEST_F(CompilerInfoCacheTest, UpdateOlderCompilerInfo)
{
  const std::string valid_hash = "valid_hash";
  FileStat valid_filestat;
  valid_filestat.mtime = 1234567;

  HashCheckingCompilerInfoValidator* validator =
        new HashCheckingCompilerInfoValidator();
  SetValidator(validator);  // valiadtor is owned by the callee.
  validator->SetLocalCompilerFileStat(valid_filestat);
  validator->SetLocalCompilerHash(valid_hash);

  std::vector<string> args;
  args.push_back("/usr/bin/gcc");
  std::unique_ptr<CompilerFlags> flags(
      CompilerFlagsParser::MustNew(args, "/tmp"));
  std::vector<string> key_env;

  CompilerInfoCache::Key key(CompilerInfoCache::CreateKey(
      *flags, "/usr/bin/gcc", key_env));
  ScopedCompilerInfoState cis(cache_->Lookup(key));
  EXPECT_TRUE(cis.get() == nullptr);

  std::vector<string> old_args;
  old_args.push_back("/usr/bin/oldgcc");
  std::unique_ptr<CompilerFlags> old_flags(
      CompilerFlagsParser::MustNew(old_args, "/tmp"));
  std::vector<string> old_key_env;

  CompilerInfoCache::Key old_key(CompilerInfoCache::CreateKey(
      *old_flags, "/usr/bin/oldgcc", old_key_env));
  ScopedCompilerInfoState old_cis(cache_->Lookup(old_key));
  EXPECT_TRUE(old_cis.get() == nullptr);

  // Set valid compiler info.
  {
    std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
    cid->set_last_used_at(time(nullptr));
    cid->set_found(true);
    cid->mutable_local_compiler_stat()->set_mtime(valid_filestat.mtime);
    cid->mutable_local_compiler_stat()->set_size(valid_filestat.size);
    cid->set_local_compiler_hash(valid_hash);
    cid->set_hash(valid_hash);
    cid->mutable_cxx();

    cis.reset(cache_->Store(key, std::move(cid)));
    EXPECT_EQ(2, cis.get()->refcnt());  // caller & in cache
  }

  // Set old compiler info.
  {
    std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
    // created 31 days ago
    cid->set_last_used_at(time(nullptr) - 60 * 60 * 24 * 31);
    cid->set_found(true);
    cid->mutable_local_compiler_stat()->set_mtime(valid_filestat.mtime);
    cid->mutable_local_compiler_stat()->set_size(valid_filestat.size);
    cid->set_local_compiler_hash(valid_hash);
    cid->set_hash(valid_hash);
    cid->mutable_cxx();

    old_cis.reset(cache_->Store(old_key, std::move(cid)));
    EXPECT_EQ(2, old_cis.get()->refcnt());  // caller & in cache
  }

  // Now Cache should be valid.
  {
    UpdateOlderCompilerInfo();

    ScopedCompilerInfoState tmp(cache_->Lookup(key));
    EXPECT_TRUE(tmp.get() != nullptr);

    ScopedCompilerInfoState old_tmp(cache_->Lookup(old_key));
    EXPECT_TRUE(old_tmp.get() == nullptr);
  }

  {
    // Change local compiler FileStat. (= changed local file timestamp.)
    FileStat changed_filestat(valid_filestat);
    changed_filestat.mtime += 1000;
    validator->SetLocalCompilerFileStat(changed_filestat);

    // Even FileStat is changed, file hash is the same, CompilerInfo
    // will be taken.
    UpdateOlderCompilerInfo();

    ScopedCompilerInfoState tmp(cache_->Lookup(key));
    EXPECT_TRUE(tmp.get() != nullptr);
  }

  {
    // Change FileStat & Hash
    FileStat changed_filestat(valid_filestat);
    changed_filestat.mtime += 2000;
    validator->SetLocalCompilerFileStat(changed_filestat);
    validator->SetLocalCompilerHash("unexpected_hash");

    // Since FileStat and file hash are both changed,
    // cache should be removed.
    UpdateOlderCompilerInfo();

    ScopedCompilerInfoState tmp(cache_->Lookup(key));
    EXPECT_TRUE(tmp.get() == nullptr);
  }
}

TEST_F(CompilerInfoCacheTest, Clear) {
  CompilerInfoDataTable table;
  CompilerInfoDataTable::Entry* entry = table.add_compiler_info_data();
  entry->add_keys("/usr/bin/gcc -O2 @");
  entry->add_keys("/usr/bin/gcc -O2 -fno-diagnostics-show-option @");
  CompilerInfoData* data = entry->mutable_data();
  data->set_name("gcc");
  data->set_lang("c");
  data->set_found(true);
  data->mutable_cxx();

  entry = table.add_compiler_info_data();
  entry->add_keys("/usr/bin/g++ -O2 @");
  data = entry->mutable_data();
  data->set_name("g++");
  data->set_lang("c++");
  data->set_found(true);
  data->mutable_cxx();

  EXPECT_TRUE(Unmarshal(table));

  EXPECT_FALSE(compiler_info().empty());
  EXPECT_FALSE(keys_by_hash().empty());

  Clear();
  EXPECT_TRUE(compiler_info().empty());
  EXPECT_TRUE(keys_by_hash().empty());
}

TEST_F(CompilerInfoCacheTest, NoLanguageExtension) {
  CompilerInfoDataTable table;
  CompilerInfoDataTable::Entry* entry = table.add_compiler_info_data();
  entry->add_keys("/usr/bin/gcc -O2 @");
  entry->add_keys("/usr/bin/gcc -O2 -fno-diagnostics-show-option @");
  CompilerInfoData* data = entry->mutable_data();
  data->set_name("gcc");
  data->set_lang("c");
  data->set_found(true);

  entry = table.add_compiler_info_data();
  entry->add_keys("/usr/bin/g++ -O2 @");
  data = entry->mutable_data();
  data->set_name("g++");
  data->set_lang("c++");
  data->set_found(true);

  EXPECT_TRUE(Unmarshal(table));

  EXPECT_TRUE(compiler_info().empty());
  EXPECT_TRUE(keys_by_hash().empty());
}

// TODO: add tests for Load and Save.

#ifdef __linux__
TEST_F(CompilerInfoCacheTest, RelativePathCompiler) {
  InstallReadCommandOutputFunc(ReadCommandOutputByPopen);
  TmpdirUtil tmpdir_util("compiler_info_cache_unittest");
  tmpdir_util.SetCwd("");

  static const char kCompilerInfoCache[] = "compiler_info_cache";

  CompilerInfoCache::Init(tmpdir_util.tmpdir(), kCompilerInfoCache, 3600);
  CompilerInfoBuilderFacade cib;
  const std::vector<string> empty_env;
  CompilerInfoCache::Key key1, key2, key3;

  {
    std::vector<string> args { "usr/bin/gcc" };
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, "/"));
    std::unique_ptr<CompilerInfoData> cid(
        cib.FillFromCompilerOutputs(*flags, "usr/bin/gcc", empty_env));
    EXPECT_NE(nullptr, cid);
    key1 = CompilerInfoCache::CreateKey(
        *flags, "usr/bin/gcc", empty_env);

    CompilerInfoCache::instance()->Store(
        key1, std::move(cid));
  }

  {
    std::vector<string> args { "../usr/bin/gcc" };
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, "/bin"));
    std::unique_ptr<CompilerInfoData> cid(
        cib.FillFromCompilerOutputs(*flags, "../usr/bin/gcc", empty_env));
    EXPECT_NE(nullptr, cid);
    key2 = CompilerInfoCache::CreateKey(
        *flags, "../usr/bin/gcc", empty_env);

    CompilerInfoCache::instance()->Store(
        key2, std::move(cid));
  }

  {
    std::vector<string> args { "/usr/bin/gcc" };
    std::unique_ptr<CompilerFlags> flags(
        CompilerFlagsParser::MustNew(args, tmpdir_util.cwd()));
    std::unique_ptr<CompilerInfoData> cid(
        cib.FillFromCompilerOutputs(*flags, "/usr/bin/gcc", empty_env));
    EXPECT_NE(nullptr, cid);
    key3 = CompilerInfoCache::CreateKey(
        *flags, "/usr/bin/gcc", empty_env);

    CompilerInfoCache::instance()->Store(
        key3, std::move(cid));
  }

  EXPECT_EQ(3, CompilerInfoCache::instance()->NumStores());
  EXPECT_EQ(0, CompilerInfoCache::instance()->NumStoreDups());
  CompilerInfoCache::Quit();

  ASSERT_TRUE(Chdir("/"));

  CompilerInfoCache::Init(tmpdir_util.tmpdir(), kCompilerInfoCache, 3600);

  EXPECT_NE(nullptr, CompilerInfoCache::instance()->Lookup(key1));
  EXPECT_NE(nullptr, CompilerInfoCache::instance()->Lookup(key2));
  EXPECT_NE(nullptr, CompilerInfoCache::instance()->Lookup(key3));

  CompilerInfoCache::Quit();
}
#endif  // __linux__

}  // namespace devtools_goma
