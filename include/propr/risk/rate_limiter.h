#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "propr/core/clock.h"

namespace propr::risk {

// Token bucket with TWO separate budgets:
//   - "normal" — strategies and order placement, capped at normal_per_min/sec.
//   - "reserved" — only the kill switch / flatten path may draw on this.
// Both refill on a 1-second cadence.
class RateLimiter {
 public:
  RateLimiter(int normal_per_min, int reserved_per_min, const core::Clock& clock);

  enum class Class { Normal, Reserved };

  // Try to take one token. Returns false if no tokens available.
  bool try_take(Class c);

  // Read current bucket levels (for telemetry).
  int normal_tokens() const;
  int reserved_tokens() const;

 private:
  void refill_locked_(std::int64_t now_ns);

  const core::Clock& clock_;
  const double normal_refill_per_ns_;
  const double reserved_refill_per_ns_;
  const double normal_capacity_;
  const double reserved_capacity_;

  mutable std::mutex mu_;
  double normal_{};
  double reserved_{};
  std::int64_t last_refill_ns_{0};
};

}  // namespace propr::risk
