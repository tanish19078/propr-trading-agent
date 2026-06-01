#include <gtest/gtest.h>

#include "propr/core/clock.h"

using propr::core::Clock;

namespace {
constexpr std::int64_t kNsPerSecond = 1'000'000'000LL;
constexpr std::int64_t kNsPerDay = 86'400LL * kNsPerSecond;

// 2026-05-29 12:00:00 UTC == day index 20602 since epoch
constexpr std::int64_t kDay20602Ns = 20602LL * kNsPerDay;
constexpr std::int64_t kNoonOnDay20602 = kDay20602Ns + 12 * 3600 * kNsPerSecond;
}  // namespace

TEST(ClockTest, NextMidnightIsAlwaysFuture) {
  const auto m = Clock::next_utc_midnight_ns(kNoonOnDay20602);
  EXPECT_GT(m, kNoonOnDay20602);
  EXPECT_EQ(m, kDay20602Ns + kNsPerDay);
}

TEST(ClockTest, LastMidnightLandsOnDayBoundary) {
  const auto m = Clock::last_utc_midnight_ns(kNoonOnDay20602);
  EXPECT_EQ(m, kDay20602Ns);
}

TEST(ClockTest, MidnightSelfIsConsidered_Past) {
  // If now is exactly midnight, last == now and next == now + 1 day.
  const auto last = Clock::last_utc_midnight_ns(kDay20602Ns);
  const auto next = Clock::next_utc_midnight_ns(kDay20602Ns);
  EXPECT_EQ(last, kDay20602Ns);
  EXPECT_EQ(next, kDay20602Ns + kNsPerDay);
}
