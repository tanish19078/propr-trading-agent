// donchian_breakout - the classic turtle channel breakout.
//
// Rules:
//   - Track the highest high and lowest low of the last `channel_window` ticks
//     (excluding the current mark).
//   - LONG when price breaks above the channel high.
//   - SHORT when price breaks below the channel low.
//   - Stop at the opposite side of a shorter `exit_window` channel, expressed as
//     a fraction of the channel width (`stop_frac_bps`). Take-profit at
//     `tp_to_stop_x` times the stop distance.
//   - One position at a time, with a cooldown. No MA filter — pure breakout.

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
  int channel_window{200};   // entry channel lookback (ticks)
  int stop_frac_bps{5000};   // stop distance = stop_frac * channel width (5000 = 50%)
  int tp_to_stop_x{200};     // take-profit as a multiple of stop distance (200 = 2.0R)
  int min_width_bps{10};     // skip if channel width (as bps of price) below this
  Qty quantity_nano{100000};
  int cooldown_ticks{30};
};

class DonchianBreakout final : public Strategy {
 public:
  const char* name() const override { return "donchian_breakout"; }

  bool on_init(const std::string& params_path) override {
    try {
      auto y = YAML::LoadFile(params_path);
      params_.asset_base = y["asset_base"].as<std::string>(params_.asset_base);
      params_.channel_window = y["channel_window"].as<int>(params_.channel_window);
      params_.stop_frac_bps = y["stop_frac_bps"].as<int>(params_.stop_frac_bps);
      params_.tp_to_stop_x = y["tp_to_stop_x"].as<int>(params_.tp_to_stop_x);
      params_.min_width_bps = y["min_width_bps"].as<int>(params_.min_width_bps);
      params_.quantity_nano =
          static_cast<Qty>(y["quantity_nano"].as<long long>(params_.quantity_nano));
      params_.cooldown_ticks = y["cooldown_ticks"].as<int>(params_.cooldown_ticks);
    } catch (const std::exception&) {
      return false;
    }
    return params_.channel_window > 1;
  }

  std::optional<IntentV1> on_market(const MarketSnapshot& snap) override {
    ++ticks_since_last_entry_;
    const auto it = snap.by_base.find(params_.asset_base);
    if (it == snap.by_base.end()) return std::nullopt;
    const auto& q = it->second.recent_marks;
    const std::size_t w = static_cast<std::size_t>(params_.channel_window);
    if (q.size() < w + 1) return std::nullopt;

    // Channel over the window EXCLUDING the current mark.
    double hi = 0.0, lo = 1e300;
    for (std::size_t i = q.size() - 1 - w; i < q.size() - 1; ++i) {
      const double v = static_cast<double>(q[i]);
      hi = std::max(hi, v);
      lo = std::min(lo, v);
    }
    const double mark = static_cast<double>(it->second.mark);
    const double width = hi - lo;

    const double width_bps = (width / std::max(mark, 1.0)) * 10000.0;
    if (width_bps < params_.min_width_bps) return std::nullopt;

    if (ticks_since_last_entry_ < params_.cooldown_ticks) return std::nullopt;

    const bool long_ok = mark > hi;
    const bool short_ok = mark < lo;
    if (!long_ok && !short_ok) return std::nullopt;

    ticks_since_last_entry_ = 0;
    return make_intent_(snap.at_ns, long_ok, mark, width);
  }

  void on_fill(const propr::schemas::v1::FillV1&) override {}
  void on_position(const propr::schemas::v1::PositionUpdateV1&) override {}
  void on_shutdown() override {}

 private:
  IntentV1 make_intent_(propr::core::Nanos at_ns, bool is_long, double mark,
                        double width) const {
    IntentV1 i;
    i.intent_uuid = "";
    i.strategy_name = name();
    i.kind = is_long ? IntentKindV1::OpenLong : IntentKindV1::OpenShort;
    i.asset_base = params_.asset_base;
    i.quantity_nano = params_.quantity_nano;
    i.suggested_entry_price_micro = static_cast<Price>(mark);

    const double stop_dist = width * (params_.stop_frac_bps / 10000.0);
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

extern "C" Strategy* create_strategy() { return new DonchianBreakout(); }
extern "C" void destroy_strategy(Strategy* s) { delete s; }
