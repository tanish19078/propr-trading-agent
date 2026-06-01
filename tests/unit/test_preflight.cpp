#include <gtest/gtest.h>

#include "propr/app/preflight.h"

using propr::app::Preflight;

TEST(PreflightTest, AllGreenReturnsNullopt) {
  Preflight p;
  p.add("a", [] { return true; });
  p.add("b", [] { return true; });
  EXPECT_FALSE(p.run().has_value());
}

TEST(PreflightTest, FirstFailureStops) {
  int b_calls = 0;
  Preflight p;
  p.add("a", [] { return false; }, [] { return "a failed"; });
  p.add("b", [&] { ++b_calls; return true; });
  auto fail = p.run();
  ASSERT_TRUE(fail.has_value());
  EXPECT_EQ(fail->gate, "a");
  EXPECT_EQ(fail->detail, "a failed");
  EXPECT_EQ(b_calls, 0);  // we never evaluated b
}

TEST(PreflightTest, OrderMattersAndPropagates) {
  Preflight p;
  p.add("a", [] { return true; });
  p.add("b", [] { return false; }, [] { return "b broken"; });
  p.add("c", [] { return true; });
  auto fail = p.run();
  ASSERT_TRUE(fail.has_value());
  EXPECT_EQ(fail->gate, "b");
  EXPECT_EQ(fail->detail, "b broken");
}
