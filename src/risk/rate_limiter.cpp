#include "propr/risk/rate_limiter.h"

namespace propr::risk {

namespace {
constexpr double kNsPerSecond = 1e9;
constexpr double kSecondsPerMinute = 60.0;
}  // namespace

RateLimiter::RateLimiter(int normal_per_min, int reserved_per_min, const core::Clock& clock)
    : clock_(clock),
      normal_refill_per_ns_(static_cast<double>(normal_per_min) /
                            (kSecondsPerMinute * kNsPerSecond)),
      reserved_refill_per_ns_(static_cast<double>(reserved_per_min) /
                              (kSecondsPerMinute * kNsPerSecond)),
      normal_capacity_(static_cast<double>(normal_per_min)),
      reserved_capacity_(static_cast<double>(reserved_per_min)),
      normal_(static_cast<double>(normal_per_min)),
      reserved_(static_cast<double>(reserved_per_min)),
      last_refill_ns_(clock.now_ns()) {}

void RateLimiter::refill_locked_(std::int64_t now_ns) {
  const std::int64_t delta = now_ns - last_refill_ns_;
  if (delta <= 0) return;
  const auto dd = static_cast<double>(delta);
  normal_ = std::min(normal_capacity_, normal_ + dd * normal_refill_per_ns_);
  reserved_ = std::min(reserved_capacity_, reserved_ + dd * reserved_refill_per_ns_);
  last_refill_ns_ = now_ns;
}

bool RateLimiter::try_take(Class c) {
  std::lock_guard<std::mutex> g(mu_);
  refill_locked_(clock_.now_ns());
  double& bucket = (c == Class::Normal) ? normal_ : reserved_;
  if (bucket < 1.0) return false;
  bucket -= 1.0;
  return true;
}

int RateLimiter::normal_tokens() const {
  std::lock_guard<std::mutex> g(mu_);
  return static_cast<int>(normal_);
}
int RateLimiter::reserved_tokens() const {
  std::lock_guard<std::mutex> g(mu_);
  return static_cast<int>(reserved_);
}

}  // namespace propr::risk
