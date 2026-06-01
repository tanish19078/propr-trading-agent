#include "propr/core/clock.h"

namespace propr::core {

namespace {
constexpr std::int64_t kNsPerSecond = 1'000'000'000;
constexpr std::int64_t kSecondsPerDay = 86'400;
constexpr std::int64_t kNsPerDay = kSecondsPerDay * kNsPerSecond;
}  // namespace

std::int64_t SystemClock::now_ns() const {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

std::int64_t Clock::last_utc_midnight_ns(std::int64_t from_ns) {
  // Unix epoch is 1970-01-01 00:00:00 UTC, so integer-divide by ns-per-day.
  // Floor division for negative values: not relevant here, we are post-1970.
  return (from_ns / kNsPerDay) * kNsPerDay;
}

std::int64_t Clock::next_utc_midnight_ns(std::int64_t from_ns) {
  const std::int64_t midnight = last_utc_midnight_ns(from_ns);
  return midnight + kNsPerDay;
}

}  // namespace propr::core
