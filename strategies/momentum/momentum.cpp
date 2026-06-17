// momentum - trend following with a breakout trigger and a volatility-scaled stop.
//
// Rules:
//   - Maintain a fast and slow simple moving average over recent marks.
//   - LONG when the fast MA is above the slow MA AND price breaks above the
//     highest mark of the last `breakout_window` ticks (Donchian-style confirm).
//   - SHORT symmetrically: fast < slow AND price breaks the recent low.
//   - Stop is `atr_stop_x` ATRs away from entry; take-profit at
//     `tp_to_stop_x` times the stop distance (a fixed reward:risk).
//   - Gate: skip entries when realised vol is below `min_vol_bps` (no trend to
//     ride in dead-flat tape) — the mirror image of range_mr's high-vol veto.
//   - One entry at a time. Cooldown between entries. No averaging, no grids.

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
  int breakout_window{100};
  int atr_window{100};
  int atr_stop_x{200};       // basis points (100 = 1.0 ATR)
  int tp_to_stop_x{150};     // reward:risk in bps (150 = 1.5R)
  int min_vol_bps{8};        // skip if realised vol below this
  Qty quantity_nano{100000};
  int cooldown_ticks{30};
};

class Momentum final : public Strategy {
 public:
  const char* name() const override { return "momentum"; }

  bool on_init(const std::string& params_path) override {
    try {
      auto y = YAML::LoadFile(params_path);
      params_.asset_base = y["asset_base"].as<std::string>(params_.asset_base);
      params_.fast_ma = y["fast_ma"].as<int>(params_.fast_ma);
      params_.slow_ma = y["slow_ma"].as<int>(params_.slow_ma);
      params_.breakout_window = y["breakout_window"].as<int>(params_.breakout_window);
      params_.atr_window = y["atr_window"].as<int>(params_.atr_window);
      params_.atr_stop_x = y["atr_stop_x"].as<int>(params_.atr_stop_x);
      params_.tp_to_stop_x = y["tp_to_stop_x"].as<int>(params_.tp_to_stop_x);
      params_.min_vol_bps = y["min_vol_bps"].as<int>(params_.min_vol_bps);
      params_.quantity_nano =
          static_cast<Qty>(y["quantity_nano"].as<long long>(params_.quantity_nano));
      params_.cooldown_ticks = y["cooldown_ticks"].as<int>(params_.cooldown_ticks);
    } catch (const std::exception&) {
      return false;
    }
    if (params_.fast_ma >= params_.slow_ma) return false;  // misconfigured
    return true;
  }

  std::optional<IntentV1> on_market(const MarketSnapshot& snap) override {
    ++ticks_since_last_entry_;
    const auto it = snap.by_base.find(params_.asset_base);
    if (it == snap.by_base.end()) return std::nullopt;
    const auto& q = it->second.recent_marks;
    const std::size_t need = static_cast<std::size_t>(
        std::max({params_.slow_ma, params_.breakout_window, params_.atr_window}));
    if (q.size() < need + 1) return std::nullopt;

    const double fast = mean_last_(q, params_.fast_ma);
    const double slow = mean_last_(q, params_.slow_ma);
    const double atr = atr_last_(q, params_.atr_window);
    const double mark = static_cast<double>(it->second.mark);

    // Realised-vol floor: ATR as bps of price. No trend worth chasing below it.
    const double vol_bps = (atr / std::max(mark, 1.0)) * 10000.0;
    if (vol_bps < params_.min_vol_bps) return std::nullopt;

    if (ticks_since_last_entry_ < params_.cooldown_ticks) return std::nullopt;

    // Breakout confirm over the window EXCLUDING the current mark.
    double hi = 0.0, lo = 1e300;
    const std::size_t w = static_cast<std::size_t>(params_.breakout_window);
    for (std::size_t i = q.size() - 1 - w; i < q.size() - 1; ++i) {
      const double v = static_cast<double>(q[i]);
      hi = std::max(hi, v);
      lo = std::min(lo, v);
    }

    const bool long_ok = fast > slow && mark > hi;
    const bool short_ok = fast < slow && mark < lo;
    if (!long_ok && !short_ok) return std::nullopt;

    ticks_since_last_entry_ = 0;
    return make_intent_(snap.at_ns, long_ok, mark, atr);
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

  // ATR proxy from a mark-only series: mean absolute tick-to-tick change.
  static double atr_last_(const std::deque<Price>& q, int n) {
    double sum = 0;
    const std::size_t start = q.size() - static_cast<std::size_t>(n);
    for (std::size_t i = start; i < q.size(); ++i) {
      sum += std::fabs(static_cast<double>(q[i]) - static_cast<double>(q[i - 1]));
    }
    return sum / static_cast<double>(n);
  }

  IntentV1 make_intent_(propr::core::Nanos at_ns, bool is_long, double mark,
                        double atr) const {
    IntentV1 i;
    i.intent_uuid = "";  // app/RiskEngine assigns when it journals
    i.strategy_name = name();
    i.kind = is_long ? IntentKindV1::OpenLong : IntentKindV1::OpenShort;
    i.asset_base = params_.asset_base;
    i.quantity_nano = params_.quantity_nano;
    i.suggested_entry_price_micro = static_cast<Price>(mark);

    const double stop_dist = atr * (params_.atr_stop_x / 100.0);
    const double tp_dist = stop_dist * (params_.tp_to_stop_x / 100.0);
    const double stop = is_long ? mark - stop_dist : mark + stop_dist;
    const double tp = is_long ? mark + tp_dist : mark - tp_dist;
    i.stop_loss_price_micro = static_cast<Price>(std::max(stop, 1.0));
    i.take_profit_price_micro = static_cast<Price>(std::max(tp, 1.0));
    i.emitted_at_ns = at_ns;
    return i;
  }

  Params params_;
  int ticks_since_last_entry_{1 << 20};
};

}  // namespace

extern "C" Strategy* create_strategy() { return new Momentum(); }
extern "C" void destroy_strategy(Strategy* s) { delete s; }
