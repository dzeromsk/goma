// Copyright 2014 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "filename_id_table.h"

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "prototmp/deps_cache_data.pb.h"

using std::string;

namespace devtools_goma {

TEST(FilenameIdTableTest, SaveLoad) {
  FilenameIdTable table;
  FilenameIdTable::Id id_a = table.InsertFilename("a");
  FilenameIdTable::Id id_b = table.InsertFilename("b");
  FilenameIdTable::Id id_c = table.InsertFilename("c");

  // Saves only 'a' and 'b'.
  std::set<FilenameIdTable::Id> ids;
  ids.insert(id_a);
  ids.insert(id_b);

  GomaFilenameIdTable goma_table;
  table.SaveTo(ids, &goma_table);
  table.Clear();

  EXPECT_EQ(FilenameIdTable::kInvalidId, table.ToId("a"));

  std::unordered_set<FilenameIdTable::Id> valid_ids;
  table.LoadFrom(goma_table, &valid_ids);

  EXPECT_EQ(id_a, table.ToId("a"));
  EXPECT_EQ(id_b, table.ToId("b"));
  // 'c' is not saved, so kInvalidId should be returned.
  EXPECT_EQ(FilenameIdTable::kInvalidId, table.ToId("c"));

  // id_a, and id_b should be valid. However, since id_c was removed,
  // it shouldn't be valid.
  EXPECT_GT(valid_ids.count(id_a), 0U);
  EXPECT_GT(valid_ids.count(id_b), 0U);
  EXPECT_EQ(valid_ids.count(id_c), 0U);
}

TEST(FilenameIdTableTest, LoadFailedDuplicateId) {
  FilenameIdTable table;

  GomaFilenameIdTable goma_table;
  GomaFilenameIdTableRecord* record = goma_table.add_record();
  record->set_filename("a");
  record->set_filename_id(0);

  record = goma_table.add_record();
  record->set_filename("b");
  record->set_filename_id(0);

  std::unordered_set<FilenameIdTable::Id> valid_ids;
  EXPECT_FALSE(table.LoadFrom(goma_table, &valid_ids));

  EXPECT_TRUE(valid_ids.empty());
}

TEST(FilenameIdTableTest, LoadFailedDuplicateFilename) {
  FilenameIdTable table;

  GomaFilenameIdTable goma_table;
  GomaFilenameIdTableRecord* record = goma_table.add_record();
  record->set_filename("a");
  record->set_filename_id(0);

  record = goma_table.add_record();
  record->set_filename("a");
  record->set_filename_id(1);

  std::unordered_set<FilenameIdTable::Id> valid_ids;
  EXPECT_FALSE(table.LoadFrom(goma_table, &valid_ids));

  EXPECT_TRUE(valid_ids.empty());
}

TEST(FilenameIdTableTest, Clear) {
  FilenameIdTable table;
  FilenameIdTable::Id id_a = table.InsertFilename("a");

  table.Clear();

  EXPECT_EQ("", table.ToFilename(id_a));
  EXPECT_EQ(FilenameIdTable::kInvalidId, table.ToId("a"));
}

TEST(FilenameIdTableTest, InsertFilename) {
  FilenameIdTable table;
  EXPECT_EQ(0, table.InsertFilename("a.cc"));
  EXPECT_EQ(1, table.InsertFilename("b.cc"));
  EXPECT_EQ(2, table.InsertFilename("c.cc"));

  // OK to insert the same filename again.
  EXPECT_EQ(0, table.InsertFilename("a.cc"));
  EXPECT_EQ(1, table.InsertFilename("b.cc"));
  EXPECT_EQ(2, table.InsertFilename("c.cc"));

  // empty string cannot be inserted.
  EXPECT_EQ(FilenameIdTable::kInvalidId, table.InsertFilename(""));
}

TEST(FilenameIdTableTest, DontNormalize) {
  FilenameIdTable table;
  FilenameIdTable::Id a = table.InsertFilename("/tmp/a");
  FilenameIdTable::Id b = table.InsertFilename("/tmp/a/../a");
  FilenameIdTable::Id c = table.InsertFilename("/tmp/a/../../tmp/a");

  EXPECT_NE(a, b);
  EXPECT_NE(b, c);
  EXPECT_NE(c, a);

  EXPECT_EQ("/tmp/a", table.ToFilename(a));
  EXPECT_EQ("/tmp/a/../a", table.ToFilename(b));
  EXPECT_EQ("/tmp/a/../../tmp/a", table.ToFilename(c));
}

TEST(FilenameIdTableTest, ToFilename) {
  FilenameIdTable table;
  FilenameIdTable::Id id_a = table.InsertFilename("a.cc");
  FilenameIdTable::Id id_b = table.InsertFilename("b.cc");
  FilenameIdTable::Id id_c = table.InsertFilename("c.cc");

  EXPECT_EQ("a.cc", table.ToFilename(id_a));
  EXPECT_EQ("b.cc", table.ToFilename(id_b));
  EXPECT_EQ("c.cc", table.ToFilename(id_c));

  EXPECT_EQ("", table.ToFilename(100));
  EXPECT_EQ("", table.ToFilename(200));
  EXPECT_EQ("", table.ToFilename(FilenameIdTable::kInvalidId));
}

TEST(FilenameIdTableTest, ToId) {
  FilenameIdTable table;
  FilenameIdTable::Id id_a = table.InsertFilename("a.cc");
  FilenameIdTable::Id id_b = table.InsertFilename("b.cc");
  FilenameIdTable::Id id_c = table.InsertFilename("c.cc");

  EXPECT_EQ(id_a, table.ToId("a.cc"));
  EXPECT_EQ(id_b, table.ToId("b.cc"));
  EXPECT_EQ(id_c, table.ToId("c.cc"));

  EXPECT_EQ(FilenameIdTable::kInvalidId, table.ToId("d.cc"));
  EXPECT_EQ(FilenameIdTable::kInvalidId, table.ToId(""));
}

}  // namespace devtools_goma
