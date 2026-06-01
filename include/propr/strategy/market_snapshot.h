#pragma once

#include <deque>
#include <unordered_map>

#include "propr/core/types.h"

namespace propr::strategy {

// The view of the world strategies receive on every on_market() call.
// Maintained by the App: appends ticks as they arrive, evicts beyond `window_size`.
struct MarketSnapshot {
  struct AssetState {
    core::Price mark{0};
    core::Price bid{0};
    core::Price ask{0};
    core::Nanos at_ns{0};
    // Recent ticks. Strategies derive indicators from this.
    std::deque<core::Price> recent_marks;
    std::size_t window_size{500};

    void push(core::Price p, core::Nanos t) {
      mark = p;
      at_ns = t;
      recent_marks.push_back(p);
      while (recent_marks.size() > window_size) recent_marks.pop_front();
    }
  };

  std::unordered_map<std::string, AssetState> by_base;
  core::Money equity{0};
  core::Money daily_headroom{0};
  core::Money overall_headroom{0};
  core::Nanos at_ns{0};
};

}  // namespace propr::strategy
