#include "propr/app/sim_runner.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/app/state_machine.h"
#include "propr/core/event_bus.h"
#include "propr/core/sim_clock.h"
#include "propr/core/ulid.h"
#include "propr/exec/order_manager.h"
#include "propr/log/logger.h"
#include "propr/persist/journal.h"
#include "propr/risk/kill_switch.h"
#include "propr/risk/leverage_cap.h"
#include "propr/risk/rate_limiter.h"
#include "propr/risk/risk_engine.h"
#include "propr/risk/sizing_policy.h"
#include "propr/schemas/v1.h"
#include "propr/sim/exchange_simulator.h"
#include "propr/sim/sim_executor.h"
#include "propr/strategy/market_snapshot.h"
#include "propr/strategy/plugin_loader.h"

namespace propr::app {

SimRunner::SimRunner(config::RuntimeConfig cfg, Config rc)
    : cfg_(std::move(cfg)), rc_(std::move(rc)) {}

SimRunner::~SimRunner() = default;

int SimRunner::run() {
  core::SimClock clock{1'700'000'000LL * 1'000'000'000LL};
  core::EventBus bus;
  core::Ulid ulid;

  // Synthetic 5K 1-step challenge - the sizing/kill envelope backtests use.
  account::ChallengeRules rules;
  rules.initial_balance = core::usdc(5000);
  rules.profit_target_abs = core::usdc(500);
  rules.max_overall_dd_abs = core::usdc(300);
  rules.max_daily_loss_abs = core::usdc(200);

  persist::Journal journal("logs/sim_journal.db");
  account::Account account;

  StateMachine sm;
  sm.transition(AppState::Reconciling);
  sm.transition(AppState::Live);

  risk::RateLimiter rate(cfg_.limits.normal_rate_per_min,
                         cfg_.limits.reserved_rate_per_min, clock);
  risk::LeverageCap lev(cfg_.limits.max_leverage_btc_eth,
                        cfg_.limits.max_leverage_other_crypto);
  risk::SizingPolicy sizing(risk::SizingPolicy::Caps{
      .max_risk_per_trade_bps = cfg_.limits.max_risk_per_trade_bps,
      .max_daily_headroom_use_bps = cfg_.limits.max_daily_headroom_use_bps,
      .max_overall_headroom_use_bps = cfg_.limits.max_overall_headroom_use_bps,
  });
  risk::KillSwitch kill(
      risk::KillSwitch::Tunables{
          .floating_loss_trip_bps = cfg_.limits.floating_loss_trip_bps,
          .daily_loss_trip_bps = cfg_.limits.daily_loss_trip_bps,
          .ws_blind_mode_after_ms = cfg_.limits.ws_blind_mode_after_ms,
      },
      clock);

  sim::SimConfig sim_cfg{.seed = rc_.seed,
                         .starting_balance = rules.initial_balance};
  sim::ExchangeSimulator simulator(sim_cfg, account, bus, clock);
  sim::SimExecutor exec(simulator);

  const std::string secret = "sim-session-secret";
  risk::RiskEngine engine(account, rules, sm, kill, rate, lev, sizing, ulid,
                          clock, secret,
                          {.max_slippage_bps = cfg_.limits.slippage_buffer_bps,
                           .command_ttl_ms = 5000});
  engine.set_daily_snapshot(account.equity());

  exec::OrderManager order_manager(exec, journal, sm, clock, secret);

  strategy::PluginLoader plugins;
  const auto default_entry = std::find_if(cfg_.strategies.begin(),
                                          cfg_.strategies.end(),
                                          [](const auto& s) { return s.enabled; });
  const std::string plugin =
      rc_.strategy_path.empty()
          ? (default_entry != cfg_.strategies.end() ? default_entry->plugin_path
                                                    : std::string{})
          : rc_.strategy_path;
  const std::string params =
      rc_.params_path.empty()
          ? (default_entry != cfg_.strategies.end() ? default_entry->params_path
                                                    : std::string{})
          : rc_.params_path;
  if (!plugins.load(plugin, params)) {
    std::cerr << "sim: failed to load strategy plugin " << plugin << "\n";
    return 1;
  }
  auto* strategy = plugins.loaded()[0].strategy;

  strategy::MarketSnapshot snap;
  std::mt19937_64 rng(rc_.seed ^ 0x5eed);
  std::normal_distribution<double> noise(0.0, 0.0012);
  double price = 60000.0;
  std::size_t intents = 0, approved = 0, rejected = 0;
  core::Money low_eq = account.equity();
  constexpr std::int64_t kStepNs = 30LL * 1'000'000'000LL;  // 30s per tick
  constexpr std::int64_t kDayNs = 86'400LL * 1'000'000'000LL;
  std::int64_t day = clock.now_ns() / kDayNs;

  for (int i = 0; i < rc_.ticks && sm.state() == AppState::Live; ++i) {
    // Regime-switching drift so trends and ranges both show up.
    const double drift = ((i / 500) % 2 == 0) ? 0.00005 : -0.00004;
    price *= (1.0 + drift + noise(rng));

    schemas::v1::TickV1 t;
    t.base = "BTC";
    t.mark_micro = static_cast<core::Price>(price * core::kMicroPerUnit);
    t.at_ns = clock.now_ns();
    simulator.tick(t);
    bus.drain([&](core::Event& ev) {
      std::visit(
          [&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, core::AccountUpdated>) {
              account.apply_account_update(e.balance, e.total_unrealized_pnl,
                                           e.isolated_position_margin,
                                           e.cross_position_margin,
                                           e.high_water_mark);
            }
          },
          ev);
    });

    snap.by_base["BTC"].push(t.mark_micro, t.at_ns);
    snap.at_ns = t.at_ns;
    snap.equity = account.equity();
    if (snap.equity < low_eq) low_eq = snap.equity;

    const std::int64_t today = clock.now_ns() / kDayNs;
    if (today != day) {
      engine.set_daily_snapshot(account.equity());
      day = today;
      PROPR_LOG_INFO(R"({"sim_daily_reset":true})");
    }

    if (auto trip = kill.check_floating(account, rules) ;
        trip || (trip = kill.check_daily(account, rules,
                                         engine.daily_snapshot()))) {
      journal.write_event(trip->at_ns, "kill_switch_trip",
                          R"({"reason":"sim"})");
      sm.transition(AppState::Flattening);
      order_manager.flatten_all(account);
      sm.transition(AppState::Halted);
      break;
    }

    if (sm.allows_new_entries()) {
      if (auto intent = strategy->on_market(snap)) {
        ++intents;
        if (intent->intent_uuid.empty()) intent->intent_uuid = ulid.next();
        auto d = engine.evaluate(*intent);
        if (d.outcome == schemas::v1::RiskOutcomeV1::Reject) {
          ++rejected;
        } else if (d.command.has_value()) {
          ++approved;
          auto rep = order_manager.execute(*d.command);
          if (rep.status != schemas::v1::ExecutionStatusV1::Accepted &&
              rep.status != schemas::v1::ExecutionStatusV1::Filled) {
            PROPR_LOG_WARN(std::string{R"({"sim_exec_status":)" +
                            std::to_string(static_cast<int>(rep.status)) + "}"});
          }
        }
      }
    }

    clock.advance(kStepNs);
  }

  const bool tripped = kill.armed();
  std::cout << "--- sim session ---\n"
            << "ticks_driven: " << rc_.ticks << "\n"
            << "final_state: " << to_string(sm.state()) << "\n"
            << "kill_switch_armed: " << (tripped ? "true" : "false")
            << (tripped ? " (" + kill.reason() + ")" : "") << "\n"
            << "intents: " << intents << " (approved " << approved
            << ", rejected " << rejected << ")\n"
            << "fills_emitted: " << simulator.fills_emitted()
            << ", rejections: " << simulator.rejections_emitted() << "\n"
            << "final_equity_usd: "
            << static_cast<double>(account.equity()) / core::kMicroPerUnit
            << "\n"
            << "low_equity_usd: "
            << static_cast<double>(low_eq) / core::kMicroPerUnit << "\n";
  return tripped ? 1 : 0;
}

}  // namespace propr::app
