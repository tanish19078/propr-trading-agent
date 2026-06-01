// range_mr - Bollinger fade with a realised-volatility gate.
//
// Rules:
//   - Compute rolling mean and stdev of the last N marks.
//   - If price < lower_band, emit OpenLong. Stop just outside the lower band.
//     Take profit at the mid.
//   - If price > upper_band, emit OpenShort symmetrically.
//   - Gate: refuse to enter if realised vol over the window exceeds the configured
//     ceiling. Mean reversion dies in high-vol regimes.
//   - One entry, one stop, one take-profit. No averaging. No grids.

#include <yaml-cpp/yaml.h>

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
  int bband_window{200};
  int bband_stdev_x{100};       // basis points (100 = 1.0 stdev)
  int take_profit_to_mid_bps{5};
  int hard_stop_outer_band_bps{50};
  Qty quantity_nano{100000};
  int realised_vol_max_bps{35};
  int cooldown_ticks{60};
};

double bps(double v) { return v / 10000.0; }

class RangeMR final : public Strategy {
 public:
  const char* name() const override { return "range_mr"; }

  bool on_init(const std::string& params_path) override {
    try {
      auto y = YAML::LoadFile(params_path);
      params_.asset_base = y["asset_base"].as<std::string>(params_.asset_base);
      params_.bband_window = y["bband_window"].as<int>(params_.bband_window);
      params_.bband_stdev_x = y["bband_stdev_x"].as<int>(params_.bband_stdev_x);
      params_.take_profit_to_mid_bps =
          y["take_profit_to_mid_bps"].as<int>(params_.take_profit_to_mid_bps);
      params_.hard_stop_outer_band_bps =
          y["hard_stop_outer_band_bps"].as<int>(params_.hard_stop_outer_band_bps);
      params_.quantity_nano =
          static_cast<Qty>(y["quantity_nano"].as<long long>(params_.quantity_nano));
      params_.realised_vol_max_bps =
          y["realised_vol_max_bps"].as<int>(params_.realised_vol_max_bps);
      params_.cooldown_ticks = y["cooldown_ticks"].as<int>(params_.cooldown_ticks);
    } catch (const std::exception&) {
      return false;
    }
    return true;
  }

  std::optional<IntentV1> on_market(const MarketSnapshot& snap) override {
    const auto it = snap.by_base.find(params_.asset_base);
    if (it == snap.by_base.end()) return std::nullopt;
    const auto& state = it->second;
    if (state.recent_marks.size() <
        static_cast<std::size_t>(params_.bband_window)) {
      return std::nullopt;
    }

    // Rolling mean and stdev over the window.
    double mean = 0;
    const auto& q = state.recent_marks;
    const std::size_t window = static_cast<std::size_t>(params_.bband_window);
    const std::size_t start = q.size() - window;
    for (std::size_t i = start; i < q.size(); ++i) mean += static_cast<double>(q[i]);
    mean /= static_cast<double>(window);
    double variance = 0;
    for (std::size_t i = start; i < q.size(); ++i) {
      const double d = static_cast<double>(q[i]) - mean;
      variance += d * d;
    }
    variance /= static_cast<double>(window);
    const double stdev = std::sqrt(variance);

    // Realised volatility, in basis points of mean per tick.
    const double realised_bps = (stdev / std::max(mean, 1.0)) * 10000.0;
    if (realised_bps > params_.realised_vol_max_bps) {
      ++ticks_since_last_entry_;
      return std::nullopt;
    }

    const double mult = params_.bband_stdev_x / 100.0;
    const double upper = mean + mult * stdev;
    const double lower = mean - mult * stdev;
    const double mark = static_cast<double>(state.mark);

    ++ticks_since_last_entry_;
    if (ticks_since_last_entry_ < params_.cooldown_ticks) return std::nullopt;

    if (mark <= lower) {
      ticks_since_last_entry_ = 0;
      return make_intent_(snap.at_ns, /*long=*/true, mark, mean, stdev);
    }
    if (mark >= upper) {
      ticks_since_last_entry_ = 0;
      return make_intent_(snap.at_ns, /*long=*/false, mark, mean, stdev);
    }
    return std::nullopt;
  }

  void on_fill(const propr::schemas::v1::FillV1&) override {}
  void on_position(const propr::schemas::v1::PositionUpdateV1&) override {}
  void on_shutdown() override {}

 private:
  IntentV1 make_intent_(propr::core::Nanos at_ns, bool is_long, double mark,
                        double mean, double stdev) const {
    IntentV1 i;
    i.intent_uuid = "";  // app/RiskEngine assigns when it journals
    i.strategy_name = name();
    i.kind = is_long ? IntentKindV1::OpenLong : IntentKindV1::OpenShort;
    i.asset_base = params_.asset_base;
    i.quantity_nano = params_.quantity_nano;
    i.suggested_entry_price_micro = static_cast<Price>(mark);
    const double mult = params_.bband_stdev_x / 100.0;
    const double outer = is_long ? (mean - mult * stdev) : (mean + mult * stdev);
    const double stop = is_long
                            ? outer * (1.0 - bps(params_.hard_stop_outer_band_bps))
                            : outer * (1.0 + bps(params_.hard_stop_outer_band_bps));
    const double tp = is_long
                          ? mean * (1.0 - bps(params_.take_profit_to_mid_bps))
                          : mean * (1.0 + bps(params_.take_profit_to_mid_bps));
    i.stop_loss_price_micro = static_cast<Price>(stop);
    i.take_profit_price_micro = static_cast<Price>(tp);
    i.emitted_at_ns = at_ns;
    return i;
  }

  Params params_;
  int ticks_since_last_entry_{1 << 20};  // start way above cooldown so first signal fires
};

}  // namespace

extern "C" Strategy* create_strategy() { return new RangeMR(); }
extern "C" void destroy_strategy(Strategy* s) { delete s; }
