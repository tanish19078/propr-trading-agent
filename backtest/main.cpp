// Backtest harness. Replays historical bars through the SAME risk/strategy/account
// stack as live, with a FakeAccount in place of REST/WS. Outputs a JSON report.
//
// Bracket lifecycle: each approved command registers stop/tp triggers; the
// replay loop closes the position at the trigger price when the mark crosses.
// Daily loss budget resets on UTC midnight crossings in the data's own timeline.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>

#include "fake_account.h"
#include "historical_feeder.h"
#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/app/state_machine.h"
#include "propr/config/config.h"
#include "propr/core/clock.h"
#include "propr/core/types.h"
#include "propr/core/ulid.h"
#include "propr/log/logger.h"
#include "propr/risk/kill_switch.h"
#include "propr/risk/leverage_cap.h"
#include "propr/risk/rate_limiter.h"
#include "propr/risk/risk_engine.h"
#include "propr/risk/sizing_policy.h"
#include "propr/schemas/v1.h"
#include "propr/strategy/market_snapshot.h"
#include "propr/strategy/plugin_loader.h"

using json = nlohmann::json;

namespace {

struct Bracket {
  bool is_long{true};
  core::Price stop{0};
  core::Price tp{0};
};

constexpr std::int64_t kDayNs = 86'400LL * 1'000'000'000LL;

}  // namespace

int main(int argc, char** argv) {
  std::string strategy_path, params_path, data_path;
  std::string config_path = "config/runtime.yaml";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--strategy" && i + 1 < argc) strategy_path = argv[++i];
    else if (a == "--params" && i + 1 < argc) params_path = argv[++i];
    else if (a == "--data" && i + 1 < argc) data_path = argv[++i];
    else if (a == "--config" && i + 1 < argc) config_path = argv[++i];
  }
  if (strategy_path.empty() || data_path.empty()) {
    std::cerr << "usage: propr_backtest --strategy PATH --params PATH --data CSV "
                 "[--config PATH]\n";
    return 2;
  }

  auto cfg = propr::config::load(config_path);
  propr::log::init(cfg.paths.log_path);

  propr::backtest::HistoricalFeeder feeder;
  if (!feeder.load(data_path)) {
    std::cerr << "failed to load " << data_path << "\n";
    return 1;
  }

  // Synthetic challenge rules — match a 5K 1-step.
  propr::account::ChallengeRules rules;
  rules.initial_balance = propr::core::usdc(5000);
  rules.profit_target_abs = propr::core::usdc(500);   // 10%
  rules.max_overall_dd_abs = propr::core::usdc(300);  // 6%
  rules.max_daily_loss_abs = propr::core::usdc(200);  // 4%

  propr::core::SystemClock clock;
  propr::risk::RateLimiter rate(cfg.limits.normal_rate_per_min,
                                cfg.limits.reserved_rate_per_min, clock);
  propr::risk::LeverageCap lev(cfg.limits.max_leverage_btc_eth,
                               cfg.limits.max_leverage_other_crypto);
  propr::risk::SizingPolicy sizing(propr::risk::SizingPolicy::Caps{
      .max_risk_per_trade_bps = cfg.limits.max_risk_per_trade_bps,
      .max_daily_headroom_use_bps = cfg.limits.max_daily_headroom_use_bps,
      .max_overall_headroom_use_bps = cfg.limits.max_overall_headroom_use_bps,
  });
  propr::risk::KillSwitch kill(
      propr::risk::KillSwitch::Tunables{
          .floating_loss_trip_bps = cfg.limits.floating_loss_trip_bps,
          .daily_loss_trip_bps = cfg.limits.daily_loss_trip_bps,
          .ws_blind_mode_after_ms = cfg.limits.ws_blind_mode_after_ms,
      },
      clock);

  propr::account::Account account;
  propr::backtest::FakeAccount fake({.starting_balance = rules.initial_balance},
                                    account);

  // The backtest is always LIVE: there is no network to go blind on.
  propr::app::StateMachine sm;
  sm.transition(propr::app::AppState::Reconciling);
  sm.transition(propr::app::AppState::Live);

  propr::core::Ulid ulid;
  const std::string secret = "backtest-offline-secret";
  propr::risk::RiskEngine engine(account, rules, sm, kill, rate, lev, sizing,
                                 ulid, clock, secret,
                                 {.max_slippage_bps = cfg.limits.slippage_buffer_bps,
                                  .command_ttl_ms = 5000});
  engine.set_daily_snapshot(account.equity());

  propr::strategy::PluginLoader loader;
  if (!loader.load(strategy_path, params_path)) {
    std::cerr << "failed to load strategy plugin\n";
    return 1;
  }
  auto* s = loader.loaded()[0].strategy;

  propr::strategy::MarketSnapshot snap;
  std::unordered_map<std::string, Bracket> brackets;
  std::size_t intents_total = 0, intents_approved = 0, intents_rejected = 0;
  int stops_hit = 0, tps_hit = 0, daily_resets = 0;
  bool halted = false;
  propr::core::Money low_eq = account.equity();
  std::int64_t current_day = -1;

  feeder.replay([&](const propr::core::MarketTick& t) {
    if (halted) return;

    auto& as = snap.by_base[t.asset.base];
    as.push(t.mark_price, t.at_ns);
    snap.at_ns = t.at_ns;
    fake.mark_to_tick(t.asset, t.mark_price);
    snap.equity = account.equity();
    if (snap.equity < low_eq) low_eq = snap.equity;

    // Bracket exits at trigger prices, stops before TPs (conservative order).
    if (auto it = brackets.find(t.asset.base); it != brackets.end()) {
      const Bracket& b = it->second;
      const bool stop_crossed =
          b.stop > 0 && (b.is_long ? t.mark_price <= b.stop : t.mark_price >= b.stop);
      const bool tp_crossed =
          b.tp > 0 && (b.is_long ? t.mark_price >= b.tp : t.mark_price <= b.tp);
      if (stop_crossed || tp_crossed) {
        const core::Price fill = stop_crossed ? b.stop : b.tp;
        fake.close(t.asset.base, fill);
        stops_hit += stop_crossed ? 1 : 0;
        tps_hit += tp_crossed ? 1 : 0;
        brackets.erase(it);
      }
    }

    // Daily reset on UTC midnight crossing of the DATA timeline.
    const std::int64_t day = t.at_ns / kDayNs;
    if (current_day != -1 && day != current_day) {
      engine.set_daily_snapshot(account.equity());
      ++daily_resets;
    }
    current_day = day;

    // Kill switch trips end the run, exactly like the live HALTED path.
    if (auto trip = kill.check_floating(account, rules)) {
      halted = true;
      std::cout << "kill_switch_trip: floating, detail=" << trip->detail << "\n";
      return;
    }
    if (auto trip = kill.check_daily(account, rules,
                                     engine.daily_snapshot())) {
      halted = true;
      std::cout << "kill_switch_trip: daily, detail=" << trip->detail << "\n";
      return;
    }

    auto intent = s->on_market(snap);
    if (!intent) return;
    ++intents_total;
    auto d = engine.evaluate(*intent);
    if (d.outcome == propr::schemas::v1::RiskOutcomeV1::Reject) {
      ++intents_rejected;
      return;
    }
    ++intents_approved;
    if (!d.command.has_value()) return;
    fake.simulate_entry(*d.command, t.mark_price);
    Bracket& b = brackets[d.command->asset_base];
    b.is_long = d.command->position_side == "long";
    b.stop = d.command->stop_trigger_price_micro.value_or(0);
    b.tp = d.command->tp_price_micro.value_or(0);
  });

  json report = {
      {"strategy", loader.loaded()[0].name},
      {"ticks", feeder.size()},
      {"intents_emitted", intents_total},
      {"intents_approved", intents_approved},
      {"intents_rejected", intents_rejected},
      {"stops_hit", stops_hit},
      {"tps_hit", tps_hit},
      {"closes", fake.closes()},
      {"daily_resets", daily_resets},
      {"halted_by_kill_switch", halted},
      {"final_balance_micro", fake.balance()},
      {"final_equity_micro", account.equity()},
      {"low_equity_micro", low_eq},
      {"high_water_mark_micro", account.high_water_mark()},
      {"max_drawdown_micro", account.high_water_mark() - low_eq},
  };
  const std::string out_path =
      "logs/backtest_" + std::to_string(clock.now_s()) + ".json";
  std::ofstream out(out_path);
  out << report.dump(2);
  std::cout << report.dump(2) << "\n";
  std::cout << "report → " << out_path << "\n";
  return 0;
}
