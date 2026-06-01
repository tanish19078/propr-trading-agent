// Backtest harness. Replays historical bars through the SAME risk/strategy/account
// stack as live, with a FakeAccount in place of REST/WS. Outputs a JSON report.

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <string>

#include "fake_account.h"
#include "historical_feeder.h"
#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/config/config.h"
#include "propr/core/clock.h"
#include "propr/log/logger.h"
#include "propr/risk/risk_engine.h"
#include "propr/strategy/market_snapshot.h"
#include "propr/strategy/plugin_loader.h"

using json = nlohmann::json;

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
    std::cerr << "usage: propr_backtest --strategy PATH --params PATH --data CSV\n";
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
  propr::backtest::FakeAccount fake({.starting_balance = rules.initial_balance}, account);

  propr::risk::RiskEngine engine(account, rules, kill, rate, lev, sizing);
  engine.set_daily_snapshot(account.equity());

  propr::strategy::PluginLoader loader;
  if (!loader.load(strategy_path, params_path)) {
    std::cerr << "failed to load strategy plugin\n";
    return 1;
  }
  auto* s = loader.loaded()[0].strategy;

  propr::strategy::MarketSnapshot snap;
  std::size_t intents_total = 0, intents_approved = 0, intents_rejected = 0;
  propr::core::Money low_eq = account.equity();

  feeder.replay([&](const propr::core::MarketTick& t) {
    auto& as = snap.by_base[t.asset.base];
    as.push(t.mark_price, t.at_ns);
    snap.at_ns = t.at_ns;
    fake.mark_to_tick(t.asset, t.mark_price);
    snap.equity = account.equity();
    if (snap.equity < low_eq) low_eq = snap.equity;

    auto intent = s->on_market(snap);
    if (!intent) return;
    ++intents_total;
    auto d = engine.evaluate(*intent);
    if (d.outcome == propr::risk::RiskEngine::Outcome::Reject) {
      ++intents_rejected;
      return;
    }
    ++intents_approved;
    fake.simulate_entry(d.intent, t.mark_price);
  });

  json report = {
      {"strategy", loader.loaded()[0].name},
      {"ticks", feeder.size()},
      {"intents_emitted", intents_total},
      {"intents_approved", intents_approved},
      {"intents_rejected", intents_rejected},
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
