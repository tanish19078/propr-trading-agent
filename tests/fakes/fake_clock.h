#pragma once

#include <atomic>

#include "propr/core/clock.h"

// Note: namespace is `propr::test_support`, NOT `propr::testing`. Anything named
// `testing` clashes with GoogleTest's top-level `::testing` namespace through
// argument-dependent lookup, which triggers cryptic
// "reference to 'testing' is ambiguous" errors in test files.
namespace propr::test_support {

class FakeClock final : public core::Clock {
 public:
  explicit FakeClock(std::int64_t initial_ns = 0) : ns_(initial_ns) {}
  std::int64_t now_ns() const override { return ns_.load(std::memory_order_acquire); }
  void set(std::int64_t ns) { ns_.store(ns, std::memory_order_release); }
  void advance(std::int64_t delta_ns) { ns_.fetch_add(delta_ns, std::memory_order_acq_rel); }

 private:
  std::atomic<std::int64_t> ns_;
};

}  // namespace propr::test_support
