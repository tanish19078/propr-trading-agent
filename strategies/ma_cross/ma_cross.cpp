// ma_cross - the simplest trend filter: fast/slow SMA crossover.
//
// Rules:
//   - Compute a fast and a slow simple moving average each tick.
//   - Enter LONG on a golden cross (fast crosses ABOVE slow this tick).
//   - Enter SHORT on a death cross (fast crosses BELOW slow this tick).
//   - We act only on the crossing EVENT, not on the persistent state, so we don't
//     re-enter every tick the relationship holds.
//   - Stop is `stop_bps` below/above entry; take-profit at `tp_to_stop_x` times the
//     stop distance. One position at a time, cooldown between entries.
//
// This is the baseline trend strategy: no breakout confirm, no vol gate. It exists
// to show how much the extra machinery in momentum/donchian actually buys.

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

#include "propr/strategy/strategy.h"

using propr::core::Price;
using propr::core::Qty;
using propr::schemas::v1::IntentKindV1;
using propr::schemas::v1::IntentV1;
using propr::strategy::MarketSnapshot;
using propr::strategy::Strategy;

namespace {

struct Params {
  std::string asset_base{"BTC"};
  int fast_ma{50};
  int slow_ma{200};
  int stop_bps{50};        // stop distance as bps of entry price (50 = 0.50%)
  int tp_to_stop_x{150};   // take-profit as a multiple of stop distance (150 = 1.5R)
  Qty quantity_nano{100000};
  int cooldown_ticks{30};
};

double bps(double v) { return v / 10000.0; }

class MaCross final : public Strategy {
 public:
  const char* name() const override { return "ma_cross"; }

  bool on_init(const std::string& params_path) override {
    try {
      auto y = YAML::LoadFile(params_path);
      params_.asset_base = y["asset_base"].as<std::string>(params_.asset_base);
      params_.fast_ma = y["fast_ma"].as<int>(params_.fast_ma);
      params_.slow_ma = y["slow_ma"].as<int>(params_.slow_ma);
      params_.stop_bps = y["stop_bps"].as<int>(params_.stop_bps);
      params_.tp_to_stop_x = y["tp_to_stop_x"].as<int>(params_.tp_to_stop_x);
      params_.quantity_nano =
          static_cast<Qty>(y["quantity_nano"].as<long long>(params_.quantity_nano));
      params_.cooldown_ticks = y["cooldown_ticks"].as<int>(params_.cooldown_ticks);
    } catch (const std::exception&) {
      return false;
    }
    return params_.fast_ma < params_.slow_ma;
  }

  std::optional<IntentV1> on_market(const MarketSnapshot& snap) override {
    ++ticks_since_last_entry_;
    const auto it = snap.by_base.find(params_.asset_base);
    if (it == snap.by_base.end()) return std::nullopt;
    const auto& q = it->second.recent_marks;
    if (q.size() < static_cast<std::size_t>(params_.slow_ma)) return std::nullopt;

    const double fast = mean_last_(q, params_.fast_ma);
    const double slow = mean_last_(q, params_.slow_ma);
    const int sign = fast > slow ? 1 : (fast < slow ? -1 : 0);

    const int prev = prev_sign_;
    prev_sign_ = sign;
    if (prev == 0 || sign == 0 || sign == prev) return std::nullopt;  // no cross
    if (ticks_since_last_entry_ < params_.cooldown_ticks) return std::nullopt;

    const double mark = static_cast<double>(it->second.mark);
    ticks_since_last_entry_ = 0;
    return make_intent_(snap.at_ns, /*long=*/sign > 0, mark);
  }

  void on_fill(const propr::schemas::v1::FillV1&) override {}
  void on_position(const propr::schemas::v1::PositionUpdateV1&) override {}
  void on_shutdown() override {}

 private:
  static double mean_last_(const std::deque<Price>& q, int n) {
    double sum = 0;
    const std::size_t start = q.size() - static_cast<std::size_t>(n);
    for (std::size_t i = start; i < q.size(); ++i) sum += static_cast<double>(q[i]);
    return sum / static_cast<double>(n);
  }

  IntentV1 make_intent_(propr::core::Nanos at_ns, bool is_long, double mark) const {
    IntentV1 i;
    i.intent_uuid = "";
    i.strategy_name = name();
    i.kind = is_long ? IntentKindV1::OpenLong : IntentKindV1::OpenShort;
    i.asset_base = params_.asset_base;
    i.quantity_nano = params_.quantity_nano;
    i.suggested_entry_price_micro = static_cast<Price>(mark);

    const double stop_dist = mark * bps(params_.stop_bps);
    const double tp_dist = stop_dist * (params_.tp_to_stop_x / 100.0);
    const double stop = is_long ? mark - stop_dist : mark + stop_dist;
    const double tp = is_long ? mark + tp_dist : mark - tp_dist;
    i.stop_loss_price_micro = static_cast<Price>(std::max(stop, 1.0));
    i.take_profit_price_micro = static_cast<Price>(std::max(tp, 1.0));
    i.emitted_at_ns = at_ns;
    return i;
  }

  Params params_;
  int prev_sign_{0};
  int ticks_since_last_entry_{1 << 20};
};

}  // namespace

extern "C" Strategy* create_strategy() { return new MaCross(); }
extern "C" void destroy_strategy(Strategy* s) { delete s; }
