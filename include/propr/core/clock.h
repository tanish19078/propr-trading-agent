#pragma once

#include <chrono>
#include <cstdint>

namespace propr::core {

// All time in this codebase is UTC. Period.
// Clock is an interface so tests can inject a FakeClock with a settable wall time.
class Clock {
 public:
  virtual ~Clock() = default;

  // Nanoseconds since unix epoch UTC.
  virtual std::int64_t now_ns() const = 0;

  // Convenience wrappers.
  std::int64_t now_ms() const { return now_ns() / 1'000'000; }
  std::int64_t now_s() const { return now_ns() / 1'000'000'000; }

  // Next 00:00:00 UTC strictly after `from_ns`. Used to schedule the daily reset.
  static std::int64_t next_utc_midnight_ns(std::int64_t from_ns);

  // Last 00:00:00 UTC at or before `from_ns`. Used to identify "today's bucket".
  static std::int64_t last_utc_midnight_ns(std::int64_t from_ns);
};

class SystemClock final : public Clock {
 public:
  std::int64_t now_ns() const override;
};

}  // namespace propr::core
