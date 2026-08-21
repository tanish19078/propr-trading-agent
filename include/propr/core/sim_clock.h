#pragma once

#include <atomic>

#include "propr/core/clock.h"

namespace propr::core {

// Manually-advanced logical clock for simulator-driven runs (--sim mode) where
// wall time is useless: a 2000-tick session must be able to cross UTC midnights
// and command TTLs deterministically. Not for live use.
class SimClock final : public Clock {
 public:
  explicit SimClock(std::int64_t initial_ns = 0) : ns_(initial_ns) {}
  std::int64_t now_ns() const override { return ns_.load(std::memory_order_acquire); }
  void set(std::int64_t ns) { ns_.store(ns, std::memory_order_release); }
  void advance(std::int64_t delta_ns) {
    ns_.fetch_add(delta_ns, std::memory_order_acq_rel);
  }

 private:
  std::atomic<std::int64_t> ns_;
};

}  // namespace propr::core
