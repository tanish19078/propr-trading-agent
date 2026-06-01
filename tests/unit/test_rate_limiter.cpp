#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "propr/risk/rate_limiter.h"

using propr::risk::RateLimiter;
using propr::test_support::FakeClock;

constexpr std::int64_t kNsPerSecond = 1'000'000'000LL;

TEST(RateLimiterTest, StartsFullSoBudgetAvailable) {
  FakeClock clock{0};
  RateLimiter rl(60, 30, clock);  // 60/min = 1/sec normal
  EXPECT_GT(rl.normal_tokens(), 0);
  EXPECT_GT(rl.reserved_tokens(), 0);
}

TEST(RateLimiterTest, NormalBucketDrainsToEmpty) {
  FakeClock clock{0};
  RateLimiter rl(60, 30, clock);  // 60 tokens capacity
  int taken = 0;
  while (rl.try_take(RateLimiter::Class::Normal)) ++taken;
  EXPECT_GE(taken, 50);
  EXPECT_LE(taken, 60);
  EXPECT_FALSE(rl.try_take(RateLimiter::Class::Normal));
}

TEST(RateLimiterTest, ReservedBucketIsIndependentOfNormal) {
  FakeClock clock{0};
  RateLimiter rl(60, 30, clock);
  while (rl.try_take(RateLimiter::Class::Normal)) {
  }
  // Reserved still has its own budget.
  EXPECT_TRUE(rl.try_take(RateLimiter::Class::Reserved));
}

TEST(RateLimiterTest, RefillsOverTime) {
  FakeClock clock{0};
  RateLimiter rl(60, 30, clock);  // 1 token/sec normal
  while (rl.try_take(RateLimiter::Class::Normal)) {
  }
  EXPECT_FALSE(rl.try_take(RateLimiter::Class::Normal));
  clock.advance(5 * kNsPerSecond);  // 5 seconds
  // Should now have ~5 tokens.
  int taken = 0;
  while (rl.try_take(RateLimiter::Class::Normal)) ++taken;
  EXPECT_GE(taken, 4);
  EXPECT_LE(taken, 6);
}
