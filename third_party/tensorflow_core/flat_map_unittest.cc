#include "flat_map.h"

#include <string>

#include "gtest/gtest.h"

using std::string;

namespace devtools_goma {

TEST(FlatMapTest, Basic) {
  FlatMap<string, string> m;
  m["ABC"] = "123";
  EXPECT_EQ("123", m["ABC"]);

  EXPECT_FALSE(m.emplace("ABC", "456").second);
  EXPECT_EQ("123", m["ABC"]);
}

}  // namespace devtools_goma
