#include "historical_feeder.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace propr::backtest {

bool HistoricalFeeder::load(const std::string& csv_path) {
  std::ifstream f(csv_path);
  if (!f.is_open()) return false;
  std::string line;
  std::getline(f, line);  // header
  while (std::getline(f, line)) {
    std::stringstream ss(line);
    std::string field;
    core::MarketTick t;
    std::getline(ss, field, ',');
    t.at_ns = std::stoll(field);
    std::getline(ss, field, ',');
    t.asset.base = field;
    std::getline(ss, field, ',');
    t.mark_price = std::stoll(field);
    ticks_.push_back(t);
  }
  std::sort(ticks_.begin(), ticks_.end(),
            [](const auto& a, const auto& b) { return a.at_ns < b.at_ns; });
  return !ticks_.empty();
}

void HistoricalFeeder::replay(Callback cb) const {
  for (const auto& t : ticks_) cb(t);
}

}  // namespace propr::backtest
