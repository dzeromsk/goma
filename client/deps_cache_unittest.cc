// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "deps_cache.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/time/time.h"
#include "compiler_flags.h"
#include "compiler_info.h"
#include "cxx/cxx_compiler_info.h"
#include "cxx/include_processor/include_cache.h"
#include "file_helper.h"
#include "gcc_flags.h"
#include "java/java_compiler_info.h"
#include "java_flags.h"
#include "path.h"
#include "path_resolver.h"
#include "prototmp/deps_cache_data.pb.h"
#include "subprocess.h"
#include "unittest_util.h"
#include "vc_flags.h"

using std::string;

namespace {
constexpr absl::Duration kDepsCacheAliveDuration = absl::Hours(3 * 24);
constexpr int kDepsCacheThreshold = 10;
constexpr int kDepsCacheMaxProtoSizeInMB = 64;
}

namespace devtools_goma {

class DepsCacheTest : public testing::Test {
 protected:
  typedef DepsCache::DepsHashId DepsHashId;

  void SetUp() override {
    tmpdir_ = absl::make_unique<TmpdirUtil>("deps_cache_test");
    IncludeCache::Init(32, true);
    DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                    kDepsCacheAliveDuration,
                    kDepsCacheThreshold,
                    kDepsCacheMaxProtoSizeInMB);
    dc_ = DepsCache::instance();
    CHECK(dc_ != nullptr) << "dc_ == nullptr";
    identifier_count_ = 0;
  }

  void TearDown() override {
    DepsCache::Quit();
    IncludeCache::Quit();
    tmpdir_.reset();
  }

  void SetFileStat(FileStatCache* cache,
                   const string& filename,
                   const FileStat& file_stat) {
    std::pair<FileStatCache::FileStatMap::iterator, bool> p =
        cache->file_stats_.insert(std::make_pair(filename, file_stat));
    if (!p.second)
      p.first->second = file_stat;
  }

  bool GetDepsHashId(const DepsCache::Identifier& identifier,
                     const string& filename,
                     DepsCache::DepsHashId* deps_hash_id) const {
    CHECK(identifier.has_value());

    FilenameIdTable::Id id = dc_->filename_id_table_.ToId(filename);
    if (id == FilenameIdTable::kInvalidId)
      return false;

    return dc_->GetDepsHashId(identifier, id, deps_hash_id);
  }

  bool UpdateLastUsedTime(const DepsCache::Identifier& identifier,
                          absl::optional<absl::Time> last_used_time) {
    CHECK(identifier.has_value());
    return dc_->UpdateLastUsedTime(identifier, std::move(last_used_time));
  }

  void UpdateGomaBuiltRevision() {
    const std::string deps_path =
        file::JoinPath(tmpdir_->tmpdir(), ".goma_deps");
    const std::string deps_sha256_path =
        file::JoinPath(tmpdir_->tmpdir(), ".goma_deps.sha256");

    GomaDeps goma_deps;

    // Load GomaDeps.
    {
      std::ifstream dot_goma_deps(deps_path.c_str(), std::ifstream::binary);
      ASSERT_TRUE(dot_goma_deps.is_open());
      ASSERT_TRUE(goma_deps.ParseFromIstream(&dot_goma_deps));
    }

    goma_deps.set_built_revision(goma_deps.built_revision() + "-new");

    // Save GomaDeps + .sha256
    // Without updating .sha256, integrity check will revoke the cache. That's
    // not what we wan to test.
    {
      std::ofstream dot_goma_deps(deps_path.c_str(), std::ofstream::binary);
      ASSERT_TRUE(dot_goma_deps.is_open());
      ASSERT_TRUE(goma_deps.SerializeToOstream(&dot_goma_deps));
    }

    {
      std::string sha256_str;
      ASSERT_TRUE(GomaSha256FromFile(deps_path, &sha256_str));
      ASSERT_TRUE(WriteStringToFile(sha256_str, deps_sha256_path));
    }
  }

  void UpdateIdentifierLastUsedTime(const DepsCache::Identifier& identifier,
                                    absl::Time last_used_time) {
    CHECK(identifier.has_value());

    const string& deps_path = file::JoinPath(tmpdir_->tmpdir(), ".goma_deps");

    GomaDeps goma_deps;

    // Load GomaDeps
    {
      std::ifstream dot_goma_deps(deps_path.c_str(), std::ifstream::binary);
      ASSERT_TRUE(dot_goma_deps.is_open());
      ASSERT_TRUE(goma_deps.ParseFromIstream(&dot_goma_deps));
    }

    GomaDependencyTable* table = goma_deps.mutable_dependency_table();
    for (int i = 0; i < table->record_size(); ++i) {
      GomaDependencyTableRecord* record = table->mutable_record(i);
      if (record->identifier() == identifier.value().ToHexString()) {
        record->set_last_used_time(absl::ToTimeT(last_used_time));
      }
    }

    // Save GomaDeps
    {
      std::ofstream dot_goma_deps(deps_path.c_str(), std::ofstream::binary);
      ASSERT_TRUE(dot_goma_deps.is_open());
      ASSERT_TRUE(goma_deps.SerializeToOstream(&dot_goma_deps));
    }
  }

  std::unique_ptr<CompilerInfoData> CreateBarebornCompilerInfo(
      const string& name) {
    std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
    cid->set_found(true);
    cid->set_name(name);
    cid->set_hash(name + "1234567890");
    cid->mutable_cxx();
    return cid;
  }

  FilenameIdTable::Id GetFilenameTableId(const string& filename) {
    return dc_->filename_id_table_.ToId(filename);
  }

  DepsCache::Identifier MakeDepsIdentifier(
      const CompilerInfo& compiler_info,
      const CompilerFlags& compiler_flags) {
    return DepsCache::MakeDepsIdentifier(compiler_info, compiler_flags);
  }

  bool SetDependencies(const DepsCache::Identifier& identifier,
                       const string& input_file,
                       const std::set<string>& dependencies,
                       FileStatCache* file_stat_cache) {
    return dc_->SetDependencies(identifier, tmpdir_->realcwd(), input_file,
                                dependencies, file_stat_cache);
  }

  bool GetDependencies(const DepsCache::Identifier& identifier,
                       const string& input_file,
                       std::set<string>* dependencies,
                       FileStatCache* file_stat_cache) const {
    return dc_->GetDependencies(identifier, tmpdir_->realcwd(), input_file,
                                dependencies, file_stat_cache);
  }

  void RemoveDependency(const DepsCache::Identifier& identifier) {
    return dc_->RemoveDependency(identifier);
  }

  int DepsCacheSize() const { return static_cast<int>(dc_->deps_table_size()); }

  DepsCache::Identifier MakeFreshIdentifier() {
    SHA256HashValue hash_value;
    SHA256HashValue::ConvertFromHexString(
        "1234567890123456789012345678901234567890123456789012345678901234",
        &hash_value);
    int* p = reinterpret_cast<int*>(&hash_value);
    *p = identifier_count_++;

    return DepsCache::Identifier(hash_value);
  }

  std::unique_ptr<TmpdirUtil> tmpdir_;
  DepsCache* dc_;
  int identifier_count_;
};

TEST_F(DepsCacheTest, SetGetDependencies) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    // Since identifier is not registered, we cannot utilize the dependencies
    // cache.
    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());

    // Note that deps does not contain the input file itself.
    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Second compile. We can utilize the dependency cache.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_TRUE(GetDependencies(identifier, acc, &deps, &file_stat_cache));

    std::set<string> deps_expected;
    deps_expected.insert(ah);
    EXPECT_EQ(deps_expected, deps);
  }

  // Update acc
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyopiyo");

  // Third compile.
  // Since directive hash is not changed, this should succeed.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_TRUE(GetDependencies(identifier, acc, &deps, &file_stat_cache));

    std::set<string> deps_expected;
    deps_expected.insert(ah);
    EXPECT_EQ(deps_expected, deps);
  }

  // Update acc. Update directives.
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "#define A\n"
      "piyopiyo");

  // Fourth compile. Since acc directive hash is changed,
  // GetDependencies should return false.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());
  }
}

TEST_F(DepsCacheTest, SetGetDependenciesRelative) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = "a.h";
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile(ah, "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    // Since identifier is not registered, we cannot utilize the dependencies
    // cache.
    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());

    // Note that deps does not contain the input file itself.
    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Second compile. We can utilize the dependency cache.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_TRUE(GetDependencies(identifier, acc, &deps, &file_stat_cache));

    std::set<string> deps_expected;
    deps_expected.insert(ah);
    EXPECT_EQ(deps_expected, deps);
  }

  // Update acc
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyopiyo");

  // Third compile.
  // Since directive hash is not changed, this should succeed.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_TRUE(GetDependencies(identifier, acc, &deps, &file_stat_cache));

    std::set<string> deps_expected;
    deps_expected.insert(ah);
    EXPECT_EQ(deps_expected, deps);
  }

  // Update acc. Update directives.
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "#define A\n"
      "piyopiyo");

  // Fourth compile. Since acc directive hash is changed,
  // GetDependencies should return false.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());
  }
}

TEST_F(DepsCacheTest, RemoveDependencies) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;
    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Second compile. We can utilize the dependency cache.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_TRUE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
  }

  RemoveDependency(identifier);

  // Third compile. Since we've removed identifier, we cannot utilize the cache.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
  }
}

TEST_F(DepsCacheTest, RemoveFile) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& bh = tmpdir_->FullPath("b.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori A");
  tmpdir_->CreateTmpFile("b.h", "kotori B");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "#include \"a.h\"\n"
      "#include \"b.h\"\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;
    deps.insert(ah);
    deps.insert(bh);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Second compile. We can utilize the dependency cache.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_TRUE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
  }

  // Delete b.h
  tmpdir_->RemoveTmpFile("b.h");

  // Third compile. Since we've removed a file, cache should not be used.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
  }
}

TEST_F(DepsCacheTest, Restart) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());

    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();
  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // Second compile. We can utilize the dependency cache.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_TRUE(GetDependencies(identifier, acc, &deps, &file_stat_cache));

    std::set<string> deps_expected;
    deps_expected.insert(ah);
    EXPECT_EQ(deps_expected, deps);
  }
}

TEST_F(DepsCacheTest, RestartWithFileStatUpdate) {
  const DepsCache::Identifier identifier1 = MakeFreshIdentifier();
  const DepsCache::Identifier identifier2 = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile for identifier1
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    deps.insert(ah);
    ASSERT_TRUE(SetDependencies(identifier1, acc, deps, &file_stat_cache));
  }

  // Update a.cc with same directive hash.
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyopiyo");

  // First compile for identifier2
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    deps.insert(ah);
    ASSERT_TRUE(SetDependencies(identifier2, acc, deps, &file_stat_cache));
  }

  // Here, a.cc was updated after identifer1 compile.
  // FileStat was different, but directive_hash should be the same.
  {
    DepsHashId deps_hash_id1;
    DepsHashId deps_hash_id2;
    ASSERT_TRUE(GetDepsHashId(identifier1, acc, &deps_hash_id1));
    ASSERT_TRUE(GetDepsHashId(identifier2, acc, &deps_hash_id2));

    ASSERT_EQ(deps_hash_id1.directive_hash, deps_hash_id2.directive_hash);
    ASSERT_NE(deps_hash_id1.file_stat, deps_hash_id2.file_stat);
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();
  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // DepsHashId will be updated to the latest one.
  // Here, a.cc was updated after identifer1 compile.
  // FileStat was different, but directive_hash should be the same.
  {
    DepsHashId deps_hash_id1;
    DepsHashId deps_hash_id2;
    ASSERT_TRUE(GetDepsHashId(identifier1, acc, &deps_hash_id1));
    ASSERT_TRUE(GetDepsHashId(identifier2, acc, &deps_hash_id2));

    EXPECT_EQ(deps_hash_id1.directive_hash, deps_hash_id2.directive_hash);
    EXPECT_EQ(deps_hash_id1.file_stat, deps_hash_id2.file_stat);
  }
}

TEST_F(DepsCacheTest, RestartWithDirectiveHashUpdate) {
  // NOTE: dependency
  // identifier1 -> a.h, b.h, a.cc
  // identifier2 -> a.h, a.cc

  const DepsCache::Identifier identifier1 = MakeFreshIdentifier();
  const DepsCache::Identifier identifier2 = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& bh = tmpdir_->FullPath("b.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("b.h",
      "#include <stdio.h>\n"
      "piyo");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <math.h>\n"
      "piyo");

  // First compile for identifier1
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    deps.insert(ah);
    deps.insert(bh);
    ASSERT_TRUE(SetDependencies(identifier1, acc, deps, &file_stat_cache));
  }

  // Update a.cc with different directive hash.
  tmpdir_->CreateTmpFile("a.cc",
      "#include <string.h>\n"
      "piyopiyo");

  // First compile for identifier2
  {
    FileStatCache file_stat_cache;

    // mtime might be the same as before (machine too fast).
    // So, we'd like to update mtime here to improve test stability.
    FileStat file_stat = file_stat_cache.Get(acc);
    ASSERT_TRUE(file_stat.mtime.has_value());
    *file_stat.mtime += absl::Seconds(1);
    SetFileStat(&file_stat_cache, acc, file_stat);

    std::set<string> deps;
    deps.insert(ah);
    ASSERT_TRUE(SetDependencies(identifier2, acc, deps, &file_stat_cache));
  }

  // Here, a.cc was updated after identifer1 compile.
  // Both directive_hash and file_stat were different.
  {
    DepsHashId deps_hash_id1;
    DepsHashId deps_hash_id2;
    ASSERT_TRUE(GetDepsHashId(identifier1, acc, &deps_hash_id1));
    ASSERT_TRUE(GetDepsHashId(identifier2, acc, &deps_hash_id2));

    ASSERT_NE(deps_hash_id1.directive_hash, deps_hash_id2.directive_hash);
    ASSERT_NE(deps_hash_id1.file_stat, deps_hash_id2.file_stat);
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();
  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // Since identifier1 was old, its entry will be garbage-collected.
  {
    DepsHashId deps_hash_id1;
    EXPECT_FALSE(GetDepsHashId(identifier1, ah, &deps_hash_id1));
    EXPECT_FALSE(GetDepsHashId(identifier1, bh, &deps_hash_id1));
    EXPECT_FALSE(GetDepsHashId(identifier1, acc, &deps_hash_id1));
  }

  // FilenameIdTable::Id for 'bh' should be garbage-collected.
  EXPECT_EQ(FilenameIdTable::kInvalidId, GetFilenameTableId(bh));
}

TEST_F(DepsCacheTest, RestartWithOldIdentifier) {
  const DepsCache::Identifier identifier1 = MakeFreshIdentifier();
  const DepsCache::Identifier identifier2 = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile for identifier1
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    deps.insert(ah);
    ASSERT_TRUE(SetDependencies(identifier1, acc, deps, &file_stat_cache));
  }
  // First compile for identifier2
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    deps.insert(ah);
    ASSERT_TRUE(SetDependencies(identifier2, acc, deps, &file_stat_cache));
  }

  // Change the last_used_time of identifier2
  {
    const absl::Time time_old_enough = absl::UnixEpoch();
    ASSERT_TRUE(UpdateLastUsedTime(identifier2, time_old_enough));
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();
  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // Since last_used_time of identifier2 was old,
  // it should be garbage-collected.
  // identifier1 should alive.
  {
    DepsHashId deps_hash_id;
    EXPECT_TRUE(GetDepsHashId(identifier1, ah, &deps_hash_id));
    EXPECT_TRUE(GetDepsHashId(identifier1, acc, &deps_hash_id));
    EXPECT_FALSE(GetDepsHashId(identifier2, ah, &deps_hash_id));
    EXPECT_FALSE(GetDepsHashId(identifier2, acc, &deps_hash_id));
  }

  // Restart DepsCache with updating identifier1.
  DepsCache::Quit();
  IncludeCache::Quit();

  // Update identifier1 last_used_time to time old enough.
  {
    absl::Time time_old_enough = absl::FromTimeT(0);
    UpdateIdentifierLastUsedTime(identifier1, time_old_enough);
  }

  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // All identifiers are garbage-collected.
  {
    DepsHashId deps_hash_id;
    EXPECT_FALSE(GetDepsHashId(identifier1, ah, &deps_hash_id));
    EXPECT_FALSE(GetDepsHashId(identifier1, acc, &deps_hash_id));
    EXPECT_FALSE(GetDepsHashId(identifier2, ah, &deps_hash_id));
    EXPECT_FALSE(GetDepsHashId(identifier2, acc, &deps_hash_id));
  }
}

TEST_F(DepsCacheTest, RestartWithOldIdentifierWithNegativeAliveDuration) {
  // Restart DepsCache with negative alive duration
  DepsCache::Quit();
  IncludeCache::Quit();
  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  absl::nullopt, kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  const DepsCache::Identifier identifier1 = MakeFreshIdentifier();
  const DepsCache::Identifier identifier2 = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // Add old identifiers.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    deps.insert(ah);
    ASSERT_TRUE(SetDependencies(identifier1, acc, deps, &file_stat_cache));
    ASSERT_TRUE(SetDependencies(identifier2, acc, deps, &file_stat_cache));

    const absl::Time time_old_enough = absl::UnixEpoch();
    ASSERT_TRUE(UpdateLastUsedTime(identifier1, time_old_enough));
    ASSERT_TRUE(UpdateLastUsedTime(identifier2, time_old_enough));
  }

  // Restart DepsCache with negative alive duration
  DepsCache::Quit();
  IncludeCache::Quit();
  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  absl::nullopt, kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // All identifiers should alive.
  {
    DepsHashId deps_hash_id;
    EXPECT_TRUE(GetDepsHashId(identifier1, ah, &deps_hash_id));
    EXPECT_TRUE(GetDepsHashId(identifier1, acc, &deps_hash_id));
    EXPECT_TRUE(GetDepsHashId(identifier2, ah, &deps_hash_id));
    EXPECT_TRUE(GetDepsHashId(identifier2, acc, &deps_hash_id));
  }
}

TEST_F(DepsCacheTest, RestartWithBuiltRevisionUpdate) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());

    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();

  // Change the built revision of GomaDeps.
  UpdateGomaBuiltRevision();

  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // All cache will be disposed.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
  }
}

TEST_F(DepsCacheTest, RestartWithMissingSha256) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());

    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();

  // Remove .goma_deps.sha256
  {
    const string& sha256_deps_path = file::JoinPath(tmpdir_->tmpdir(),
                                                    ".goma_deps.sha256");
    ASSERT_EQ(0, remove(sha256_deps_path.c_str()));
  }

  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // All cache will be disposed.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
  }
}

TEST_F(DepsCacheTest, RestartWithInvalidSha256) {
  const DepsCache::Identifier identifier = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  // First compile.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());

    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier, acc, deps, &file_stat_cache));
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();

  // Convert .goma_deps.sha256 to invalid one
  {
    const string& sha256_deps_path = file::JoinPath(tmpdir_->tmpdir(),
                                                    ".goma_deps.sha256");
    ASSERT_TRUE(WriteStringToFile("invalid-sha256", sha256_deps_path));
  }

  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  // All cache will be disposed.
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifier, acc, &deps, &file_stat_cache));
  }
}

TEST_F(DepsCacheTest, RestartWithUpdatedFilesInSomeIdentifier) {
  // identifier1: a.h, b.h, a.cc
  // identifier2: a.h, a.cc
  // "a.h" of identifier2 is latest and updated, but "a.h" of identifier1 is
  // older, so identidifer1 won't be saved.
  // In this case, "b.h" won't be included in FilenameIdtable.

  const DepsCache::Identifier identifier1 = MakeFreshIdentifier();
  const DepsCache::Identifier identifier2 = MakeFreshIdentifier();

  const string& ah = tmpdir_->FullPath("a.h");
  const string& bh = tmpdir_->FullPath("b.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori-a");
  tmpdir_->CreateTmpFile("b.h", "kotori-kotori-b");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");
  {
    FileStatCache file_stat_cache;

    std::set<string> deps;
    deps.insert(ah);
    deps.insert(bh);
    EXPECT_TRUE(SetDependencies(identifier1, acc, deps, &file_stat_cache));
  }

  tmpdir_->CreateTmpFile("a.h", "#include <string.h>\n");

  {
    FileStatCache file_stat_cache;
    FileStat file_stat = file_stat_cache.Get(ah);
    // Ensure it's newer than the previous.
    ASSERT_TRUE(file_stat.mtime);
    *file_stat.mtime += absl::Seconds(1);
    SetFileStat(&file_stat_cache, ah, file_stat);

    std::set<string> deps;
    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifier2, acc, deps, &file_stat_cache));
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();

  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  {
    FileStatCache file_stat_cache;
    std::set<string> deps;
    EXPECT_FALSE(GetDependencies(identifier1, acc, &deps, &file_stat_cache));
  }
  {
    FileStatCache file_stat_cache;
    std::set<string> deps;
    EXPECT_TRUE(GetDependencies(identifier2, acc, &deps, &file_stat_cache));
  }
}

TEST_F(DepsCacheTest, RestartWithLargeNumberIdentifiers) {
  const int N = 30;
  ASSERT_GT(N, kDepsCacheThreshold);

  std::vector<DepsCache::Identifier> identifiers(N);
  for (int i = 0; i < N; ++i) {
    identifiers[i] = MakeFreshIdentifier();
  }

  const string& ah = tmpdir_->FullPath("a.h");
  const string& acc = tmpdir_->FullPath("a.cc");

  tmpdir_->CreateTmpFile("a.h", "kotori");
  tmpdir_->CreateTmpFile("a.cc",
      "#include <stdio.h>\n"
      "piyo");

  for (int i = 0; i < N; ++i) {
    FileStatCache file_stat_cache;
    std::set<string> deps;

    EXPECT_FALSE(GetDependencies(identifiers[i], acc, &deps, &file_stat_cache));
    EXPECT_TRUE(deps.empty());

    deps.insert(ah);
    EXPECT_TRUE(SetDependencies(identifiers[i], acc, deps, &file_stat_cache));
  }

  // Restart DepsCache.
  DepsCache::Quit();
  IncludeCache::Quit();

  IncludeCache::Init(32, true);
  DepsCache::Init(file::JoinPath(tmpdir_->tmpdir(), ".goma_deps"),
                  kDepsCacheAliveDuration,
                  kDepsCacheThreshold,
                  kDepsCacheMaxProtoSizeInMB);
  dc_ = DepsCache::instance();

  EXPECT_EQ(kDepsCacheThreshold, DepsCacheSize());
}

TEST_F(DepsCacheTest, MakeDepsIdentifierGcc) {
  const string bare_gcc = "/usr/bin/gcc";
  const string bare_clang = "/usr/bin/clang";

  DepsCache::Identifier identifier;
  {
    std::vector<string> args;
    args.push_back("gcc");
    args.push_back("-c");
    args.push_back("test.c");

    GCCFlags flags(args, "/tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_gcc));
    identifier = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier.has_value());
  }

  DepsCache::Identifier identifier_compiler;
  {
    std::vector<string> args;
    args.push_back("clang");  // this differs.
    args.push_back("-c");
    args.push_back("test.c");

    GCCFlags flags(args, "/tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_clang));
    identifier_compiler = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_compiler.has_value());
  }

  DepsCache::Identifier identifier_filename;
  {
    std::vector<string> args;
    args.push_back("gcc");
    args.push_back("-c");
    args.push_back("test2.c");  // this differs.

    GCCFlags flags(args, "/tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_gcc));
    identifier_filename = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_filename.has_value());
  }

  DepsCache::Identifier identifier_include;
  {
    std::vector<string> args;
    args.push_back("gcc");
    args.push_back("-I/include");  // this differs.
    args.push_back("-c");
    args.push_back("test.c");

    GCCFlags flags(args, "/tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_gcc));
    identifier_include = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_include.has_value());
  }

  DepsCache::Identifier identifier_systeminclude;
  {
    std::vector<string> args;
    args.push_back("gcc");
    args.push_back("-isysteminclude");  // this differs.
    args.push_back("-c");
    args.push_back("test.c");

    GCCFlags flags(args, "/tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_gcc));
    identifier_systeminclude = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_systeminclude.has_value());
  }

  DepsCache::Identifier identifier_macro;
  {
    std::vector<string> args;
    args.push_back("gcc");
    args.push_back("-DKOTORI");  // this differs.
    args.push_back("-c");
    args.push_back("test.c");

    GCCFlags flags(args, "/tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_gcc));
    identifier_macro = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_macro.has_value());
  }

  DepsCache::Identifier identifier_cwd;
  {
    std::vector<string> args;
    args.push_back("gcc");
    args.push_back("-c");
    args.push_back("test.c");

    GCCFlags flags(args, "/tmp2");  // this differs.
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_gcc));
    identifier_cwd = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_cwd.has_value());
  }

  EXPECT_NE(identifier.value(), identifier_include.value());
  EXPECT_NE(identifier.value(), identifier_compiler.value());
  EXPECT_NE(identifier.value(), identifier_filename.value());
  EXPECT_NE(identifier.value(), identifier_systeminclude.value());
  EXPECT_NE(identifier.value(), identifier_macro.value());
  EXPECT_NE(identifier.value(), identifier_cwd.value());
}

TEST_F(DepsCacheTest, MakeDepsIdentifierVC) {
  const string bare_cl = "cl.exe";

  DepsCache::Identifier identifier;
  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back("test.c");

    VCFlags flags(args, "C:\\tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_cl));
    identifier = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier.has_value());
  }

  DepsCache::Identifier identifier_filename;
  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back("test2.c");  // this differs.

    VCFlags flags(args, "C:\\tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_cl));
    identifier_filename = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_filename.has_value());
  }

  DepsCache::Identifier identifier_include;
  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("-IC:\\include");  // this differs.
    args.push_back("/c");
    args.push_back("test.c");

    VCFlags flags(args, "C:\\tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_cl));
    identifier_include = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_include.has_value());
  }

  DepsCache::Identifier identifier_compiler;
  {
    std::vector<string> args;
    args.push_back("C:\\clang-cl.exe");  // this differs.
    args.push_back("/c");
    args.push_back("test.c");

    VCFlags flags(args, "C:\\tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo("C:\\clang-cl.exe"));
    identifier_compiler = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_compiler.has_value());
  }

  DepsCache::Identifier identifier_macro;
  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/DKOTORI");  // this differs.
    args.push_back("/c");
    args.push_back("test.c");

    VCFlags flags(args, "C:\\tmp");
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_cl));
    identifier_macro = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_macro.has_value());
  }

  DepsCache::Identifier identifier_cwd;
  {
    std::vector<string> args;
    args.push_back(bare_cl);
    args.push_back("/c");
    args.push_back("test.c");

    VCFlags flags(args, "C:\\tmp2");  // this differs.
    CxxCompilerInfo info(CreateBarebornCompilerInfo(bare_cl));
    identifier_cwd = MakeDepsIdentifier(info, flags);
    EXPECT_TRUE(identifier_cwd.has_value());
  }

  EXPECT_NE(identifier.value(), identifier_filename.value());
  EXPECT_NE(identifier.value(), identifier_include.value());
  EXPECT_NE(identifier.value(), identifier_compiler.value());
  EXPECT_NE(identifier.value(), identifier_macro.value());
  EXPECT_NE(identifier.value(), identifier_cwd.value());
}

TEST_F(DepsCacheTest, MakeDepsIdentifierJavac) {
  // TODO: Currently DepsCache for java is disabled.
  // Invalid RequiredFilesIdentifier is always returned.

  std::vector<string> args;
  args.push_back("javac");
  args.push_back("Test.java");

  JavacFlags flags(args, "/tmp");
  std::unique_ptr<CompilerInfoData> cid(new CompilerInfoData);
  cid->set_found(true);
  cid->mutable_javac();
  JavacCompilerInfo info(std::move(cid));
  DepsCache::Identifier identifier = MakeDepsIdentifier(info, flags);
  EXPECT_FALSE(identifier.has_value());
}

}  // namespace devtools_goma
