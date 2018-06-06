#include "flat_set.h"

#include <string>

#include "gtest/gtest.h"

using std::string;

namespace devtools_goma {

TEST(FlatMapTest, Basic) {
  FlatSet<string> m;

  EXPECT_TRUE(m.emplace("ABC").second);
  EXPECT_EQ(1U, m.count("ABC"));

  EXPECT_FALSE(m.emplace("ABC").second);
  EXPECT_EQ(1U, m.count("ABC"));
  EXPECT_NE(1U, m.count("123"));
}

}  // namespace devtools_goma
