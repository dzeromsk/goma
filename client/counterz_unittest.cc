// Copyright 2018 The Goma Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "counterz.h"

#include <gtest/gtest.h>

#include "absl/time/time.h"
#include "json_util.h"
MSVC_PUSH_DISABLE_WARNING_FOR_PROTO();
#include "prototmp/counterz.pb.h"
MSVC_POP_WARNING();

namespace devtools_goma {

namespace {

// Extracts Counterz stat fields from a Json Value object. Returns true if
// successful. If failed, returns false and stores error message in
// |error_message|.
bool GetCounterzStatsFromJson(const Json::Value& json,
                              std::string* name,
                              int64_t* count,
                              std::string* total_time,
                              std::string* average_time,
                              std::string* error_message) {
  return GetStringFromJson(json, "name", name, error_message) &&
         GetInt64FromJson(json, "count", count, error_message) &&
         GetStringFromJson(json, "total time", total_time, error_message) &&
         GetStringFromJson(json, "average time", average_time, error_message);
}

}  // namespace

class CounterzTest : public ::testing::Test {
 public:
  void SetUp() override { Counterz::Init(); }
  void TearDown() override { Counterz::Quit(); }

  void FillCounterzWithTestData() {
    // Populate Counterz with time values that require nanosecond resolution.

    // Add a total of 2.1 seconds to |counter1|.
    CounterInfo* counter1 =
        Counterz::Instance()->NewCounterInfo("foo.cc:123", "foo", "counter1");
    counter1->Inc(absl::Seconds(1));
    // These two add up to 1.1 seconds.
    counter1->Inc(absl::Nanoseconds(1));
    counter1->Inc(absl::Milliseconds(1100) - absl::Nanoseconds(1));

    // Add a total of 1.5 seconds to |counter2|.
    CounterInfo* counter2 =
        Counterz::Instance()->NewCounterInfo("bar.cc:123", "bar1", "counter2");
    counter2->Inc(absl::Milliseconds(499));
    counter2->Inc(absl::Seconds(1));
    // These two add up to 1 millisecond.
    counter2->Inc(absl::Microseconds(1) - absl::Nanoseconds(1));
    counter2->Inc(absl::Microseconds(999) + absl::Nanoseconds(1));

    // Add a total of 2.5 seconds to |counter3|.
    CounterInfo* counter3 =
        Counterz::Instance()->NewCounterInfo("bar.cc:456", "bar2", "counter3");
    // These three add up to 1 millisecond.
    counter3->Inc(absl::Microseconds(1) - absl::Nanoseconds(19));
    counter3->Inc(absl::Nanoseconds(19));
    counter3->Inc(absl::Microseconds(999));

    counter3->Inc(absl::Seconds(2));
    counter3->Inc(absl::Milliseconds(499));
  }
};

TEST_F(CounterzTest, EmptyJson) {
  Json::Value json;
  Counterz::Instance()->DumpToJson(&json);

  EXPECT_TRUE(json.isArray());
  EXPECT_EQ(0, json.size());
}

TEST_F(CounterzTest, EmptyProto) {
  CounterzStats proto;
  Counterz::Instance()->DumpToProto(&proto);

  EXPECT_EQ(0, proto.counterz_stats_size());
}

TEST_F(CounterzTest, DumpJson) {
  FillCounterzWithTestData();

  Json::Value json;
  Counterz::Instance()->DumpToJson(&json);

  EXPECT_TRUE(json.isArray());
  ASSERT_EQ(3, json.size());

  std::string error_message;
  // JSON values are sorted by total time, in decreasing order.
  {
    // counter3
    std::string name;
    int64_t count = -1;
    std::string total_time, average_time;

    EXPECT_TRUE(GetCounterzStatsFromJson(json[0], &name, &count, &total_time,
                                         &average_time, &error_message))
        << error_message;
    EXPECT_EQ("bar.cc:456(bar2:counter3)", name);
    EXPECT_EQ(5, count);
    EXPECT_EQ("2.5s", total_time);
    EXPECT_EQ("500ms", average_time);  // 2.5 seconds / 5
  }
  {
    // counter1
    std::string name;
    int64_t count = -1;
    std::string total_time, average_time;

    EXPECT_TRUE(GetCounterzStatsFromJson(json[1], &name, &count, &total_time,
                                         &average_time, &error_message))
        << error_message;
    EXPECT_EQ("foo.cc:123(foo:counter1)", name);
    EXPECT_EQ(3, count);
    EXPECT_EQ("2.1s", total_time);
    EXPECT_EQ("700ms", average_time);  // 2.1 seconds / 3
  }
  {
    // counter2
    std::string name;
    int64_t count = -1;
    std::string total_time, average_time;

    EXPECT_TRUE(GetCounterzStatsFromJson(json[2], &name, &count, &total_time,
                                         &average_time, &error_message))
        << error_message;
    EXPECT_EQ("bar.cc:123(bar1:counter2)", name);
    EXPECT_EQ(4, count);
    EXPECT_EQ("1.5s", total_time);
    EXPECT_EQ("375ms", average_time);  // 1.5 seconds / 4
  }
}

TEST_F(CounterzTest, DumpProto) {
  FillCounterzWithTestData();

  CounterzStats proto;
  Counterz::Instance()->DumpToProto(&proto);

  ASSERT_EQ(3, proto.counterz_stats_size());

  EXPECT_EQ("counter1", proto.counterz_stats(0).name());
  EXPECT_EQ("foo.cc:123", proto.counterz_stats(0).location());
  EXPECT_EQ("foo", proto.counterz_stats(0).function_name());
  EXPECT_EQ(3, proto.counterz_stats(0).total_count());
  EXPECT_EQ(2.1e9, proto.counterz_stats(0).total_time_ns());

  EXPECT_EQ("counter2", proto.counterz_stats(1).name());
  EXPECT_EQ("bar.cc:123", proto.counterz_stats(1).location());
  EXPECT_EQ("bar1", proto.counterz_stats(1).function_name());
  EXPECT_EQ(4, proto.counterz_stats(1).total_count());
  EXPECT_EQ(1.5e9, proto.counterz_stats(1).total_time_ns());

  EXPECT_EQ("counter3", proto.counterz_stats(2).name());
  EXPECT_EQ("bar.cc:456", proto.counterz_stats(2).location());
  EXPECT_EQ("bar2", proto.counterz_stats(2).function_name());
  EXPECT_EQ(5, proto.counterz_stats(2).total_count());
  EXPECT_EQ(2.5e9, proto.counterz_stats(2).total_time_ns());
}

}  // namespace devtools_goma
