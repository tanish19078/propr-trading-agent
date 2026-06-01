#pragma once

#include <functional>
#include <string>
#include <vector>

#include "propr/core/events.h"

namespace propr::backtest {

// Reads a CSV with columns: ts_ns,base,mark_price (micro-USDC).
// Emits MarketTick events for each row in ascending ts order.
class HistoricalFeeder {
 public:
  using Callback = std::function<void(const core::MarketTick&)>;

  bool load(const std::string& csv_path);
  void replay(Callback cb) const;

  std::size_t size() const { return ticks_.size(); }

 private:
  std::vector<core::MarketTick> ticks_;
};

}  // namespace propr::backtest
