// sim_harness.h - drive a REAL strategy plugin through the REAL risk core and
// score it on synthetic market regimes. Header-only so multiple test binaries can
// reuse it.
//
// What this exercises (the point of "evaluate via the simulator"):
//   PluginLoader -> Strategy::on_market(MarketSnapshot)
//                -> RiskEngine::evaluate (sizing, leverage, kill switch, signing)
//                -> OrderManager::execute (HMAC verify, expiry, state machine)
//                -> SimExecutor -> ExchangeSimulator::place
// The full intent->decision->command->executor path runs untouched.
//
// What the harness owns (because the simulator is long-only and never triggers
// brackets): position lifecycle and P&L. When the RiskEngine APPROVES a command we
// open a position in a local ledger at the tick mark (slippage + taker fee applied),
// then close it the instant the mark crosses the strategy's stop or take-profit.
// Shorts are modelled correctly here. The resulting equity is fed back into the
// Account mirror every tick so sizing shrinks with headroom and the kill switch can
// trip on a real adverse curve. This is harness-level P&L, deliberately parallel to
// the simulator's own (long-only) balance.

#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "fakes/fake_clock.h"
#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/app/state_machine.h"
#include "propr/core/event_bus.h"
#include "propr/core/types.h"
#include "propr/core/ulid.h"
#include "propr/exec/order_manager.h"
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

namespace propr::simharness {

// ── Synthetic market regimes ────────────────────────────────────────────────
// Deterministic given (regime, seed). Prices are micro-USDC/base.

enum class Regime { Uptrend, Downtrend, Range, HighVolChop };

inline const char* regime_name(Regime r) {
  switch (r) {
    case Regime::Uptrend: return "uptrend";
    case Regime::Downtrend: return "downtrend";
    case Regime::Range: return "range";
    case Regime::HighVolChop: return "highvol_chop";
  }
  return "?";
}

using TickSeries = std::vector<schemas::v1::TickV1>;

// ── Platform-stable randomness ───────────────────────────────────────────────
// std::normal_distribution streams are implementation-defined: MSVC and
// libstdc++ draw different sequences from the same seed, which silently gave
// Linux CI a different market than Windows dev boxes. SplitMix64 + Box-Muller
// is bit-identical on every platform, so "deterministic given seed" actually
// holds for the harness too.

inline double splitmix64_uniform(std::uint64_t& state) {
  state += 0x9E3779B97F4A7C15ULL;
  std::uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= (z >> 31);
  return static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0);  // [0, 1)
}

inline double gauss(std::uint64_t& state) {
  constexpr double kTwoPi = 6.283185307179586476925286766559;
  double u1 = splitmix64_uniform(state);
  const double u2 = splitmix64_uniform(state);
  if (u1 <= 0.0) u1 = 1e-15;
  return std::sqrt(-2.0 * std::log(u1)) * std::cos(kTwoPi * u2);
}

inline TickSeries make_regime(Regime r, std::uint64_t seed, int n = 2000,
                              double start_px = 60'000.0) {
  std::uint64_t rng_state = seed ^ 0x51ED270B46D6CA81ULL;
  TickSeries out;
  out.reserve(static_cast<std::size_t>(n));

  double px = start_px;
  const double center = start_px;
  core::Nanos t = 1'700'000'000'000'000'000LL;
  const core::Nanos dt = 1'000'000'000LL;  // 1s ticks

  for (int i = 0; i < n; ++i) {
    double drift = 0.0, sd = 0.0;
    switch (r) {
      case Regime::Uptrend:    drift = +0.00025; sd = 0.0009; break;
      case Regime::Downtrend:  drift = -0.00025; sd = 0.0009; break;
      case Regime::Range:      drift = 0.0;      sd = 0.0008; break;
      case Regime::HighVolChop: drift = 0.0;     sd = 0.0032; break;
    }
    if (r == Regime::Range) {
      // Ornstein-Uhlenbeck pull toward the center keeps it ranging.
      px += 0.02 * (center - px) + px * sd * gauss(rng_state);
    } else {
      px *= (1.0 + drift + sd * gauss(rng_state));
    }
    if (px < 1.0) px = 1.0;

    schemas::v1::TickV1 tick;
    tick.base = "BTC";
    tick.mark_micro = static_cast<core::Price>(px * core::kMicroPerUnit);
    tick.bid_micro = tick.mark_micro;
    tick.ask_micro = tick.mark_micro;
    tick.at_ns = t;
    out.push_back(tick);
    t += dt;
  }
  return out;
}

// ── Scoring ─────────────────────────────────────────────────────────────────

struct SimResult {
  std::string strategy;
  std::string regime;
  core::Money initial_equity{0};
  core::Money final_equity{0};
  core::Money net_pnl{0};
  core::Money max_drawdown{0};  // worst peak-to-trough equity, positive
  int trades{0};
  int wins{0};
  bool kill_switch_tripped{false};
  bool survived{true};  // false if the kill switch flattened the account

  double win_rate() const { return trades ? double(wins) / trades : 0.0; }
};

struct RunConfig {
  core::Money initial_balance{core::usdc(5000)};
  core::Money profit_target{core::usdc(500)};    // 10%
  core::Money max_overall_dd{core::usdc(250)};   // 5%
  core::Money max_daily_loss{core::usdc(150)};   // 3%
  int leverage_btc_eth{3};
  int leverage_other{2};
  int taker_fee_bps{8};
  int slippage_bps{5};
  std::size_t window_size{1200};  // marks retained per asset for indicators
};

namespace detail {

// One open position in the harness ledger. Long or short, with brackets.
struct LedgerPos {
  bool is_long{true};
  core::Qty qty{0};               // nano-base
  core::Price entry{0};           // micro, actual fill incl. slippage
  std::optional<core::Price> stop;
  std::optional<core::Price> tp;
};

// Fill price for an entry/exit, with slippage pushed against us.
inline core::Price slip(core::Price mark, bool buy, int bps) {
  const double f = bps / 10000.0;
  return buy ? static_cast<core::Price>(mark * (1.0 + f))
             : static_cast<core::Price>(mark * (1.0 - f));
}

inline core::Money fee_on(core::Price price, core::Qty qty, int bps) {
  return (core::notional(price, qty) * bps) / 10000;
}

// Signed unrealized P&L of an open position at `mark`, micro-USDC.
inline core::Money upnl(const LedgerPos& p, core::Price mark) {
  const core::Money move = core::notional(mark, p.qty) - core::notional(p.entry, p.qty);
  return p.is_long ? move : -move;
}

}  // namespace detail

// Run one strategy plugin over one tick series. Self-contained: builds a fresh
// risk core, journal (temp), simulator, and ledger per call.
inline SimResult run_strategy_sim(const std::string& plugin_path,
                                  const std::string& params_path,
                                  const TickSeries& ticks,
                                  const std::string& regime,
                                  const std::string& journal_db,
                                  const RunConfig& rc = {}) {
  using namespace propr;

  test_support::FakeClock clock{ticks.empty() ? 0 : ticks.front().at_ns};
  core::EventBus bus;
  core::Ulid ulid;
  persist::Journal journal(journal_db);

  account::Account account;
  account.set_id(core::AccountId{"harness_acct"});
  account.apply_account_update(rc.initial_balance, 0, 0, 0, rc.initial_balance);

  account::ChallengeRules rules;
  rules.initial_balance = rc.initial_balance;
  rules.profit_target_abs = rc.profit_target;
  rules.max_overall_dd_abs = rc.max_overall_dd;
  rules.max_daily_loss_abs = rc.max_daily_loss;
  rules.drawdown_type = "static";

  app::StateMachine sm;
  sm.transition(app::AppState::Reconciling);

  risk::RateLimiter rate(100000, 50000, clock);  // generous; not what we test
  risk::LeverageCap lev(rc.leverage_btc_eth, rc.leverage_other);
  risk::SizingPolicy sizing;
  risk::KillSwitch kill({.floating_loss_trip_bps = 7000, .daily_loss_trip_bps = 7000},
                        clock);

  // The simulator is wired in so the executor path is real, even though the
  // harness ledger owns P&L. Faults off: we score the strategy, not the network.
  sim::ExchangeSimulator simulator({.starting_balance = rc.initial_balance,
                                    .account_id = "harness_acct"},
                                   account, bus, clock);
  sim::SimExecutor sim_exec(simulator);

  const std::string secret = "harness-secret";
  risk::RiskEngine engine(account, rules, sm, kill, rate, lev, sizing, ulid, clock,
                          secret, {.max_slippage_bps = rc.slippage_bps,
                                   .command_ttl_ms = 5000});
  engine.set_daily_snapshot(account.equity());
  exec::OrderManager om(sim_exec, journal, sm, clock, secret);

  strategy::PluginLoader loader;
  SimResult res;
  res.strategy = "<load-failed>";
  res.regime = regime;
  res.initial_equity = rc.initial_balance;
  res.final_equity = rc.initial_balance;
  if (!loader.load(plugin_path, params_path) || loader.loaded().empty()) {
    return res;  // strategy name stays <load-failed>; caller asserts on it
  }
  strategy::Strategy* strat = loader.loaded().front().strategy;
  res.strategy = strat->name();

  sm.transition(app::AppState::Live);

  strategy::MarketSnapshot snap;
  core::Money balance = rc.initial_balance;
  core::Money hwm = rc.initial_balance;
  core::Money peak_equity = rc.initial_balance;
  std::optional<detail::LedgerPos> pos;  // one position per asset (BTC only here)

  auto mark_now = [&](void) -> core::Price {
    auto it = snap.by_base.find("BTC");
    return it == snap.by_base.end() ? 0 : it->second.mark;
  };

  auto close_pos = [&](core::Price mark) {
    if (!pos) return;
    const core::Price fill = detail::slip(mark, /*buy=*/!pos->is_long, rc.slippage_bps);
    const core::Money pnl = pos->is_long
                                ? core::notional(fill, pos->qty) -
                                      core::notional(pos->entry, pos->qty)
                                : core::notional(pos->entry, pos->qty) -
                                      core::notional(fill, pos->qty);
    balance += pnl - detail::fee_on(fill, pos->qty, rc.taker_fee_bps);
    if (pnl > 0) ++res.wins;
    pos.reset();
  };

  auto push_equity = [&](core::Price mark) {
    const core::Money u = pos ? detail::upnl(*pos, mark) : 0;
    const core::Money eq = balance + u;
    if (eq > hwm) hwm = eq;
    account.apply_account_update(balance, u, 0, 0, hwm);
    if (eq > peak_equity) peak_equity = eq;
    const core::Money dd = peak_equity - eq;
    if (dd > res.max_drawdown) res.max_drawdown = dd;
  };

  for (const auto& t : ticks) {
    clock.set(t.at_ns);
    auto& st = snap.by_base["BTC"];
    st.window_size = rc.window_size;
    st.push(t.mark_micro, t.at_ns);
    simulator.tick(t);
    bus.drain([](core::Event&) {});  // keep the bus from growing unbounded

    const core::Price mark = mark_now();

    // 1. Bracket exits: close the open position if the mark crossed stop or TP.
    if (pos) {
      const bool hit_stop =
          pos->stop && (pos->is_long ? mark <= *pos->stop : mark >= *pos->stop);
      const bool hit_tp =
          pos->tp && (pos->is_long ? mark >= *pos->tp : mark <= *pos->tp);
      if (hit_stop || hit_tp) close_pos(mark);
    }

    // 2. Equity feedback BEFORE asking the strategy / risk engine to act.
    snap.equity = balance + (pos ? detail::upnl(*pos, mark) : 0);
    snap.overall_headroom =
        std::max<core::Money>(snap.equity - rules.dd_floor_from(hwm), 0);
    snap.daily_headroom = std::max<core::Money>(
        snap.equity - rules.daily_floor_from(engine.daily_snapshot()), 0);
    snap.at_ns = t.at_ns;
    push_equity(mark);

    // 3. Kill switch — the survival bar. Equity-based, on the harness curve.
    if (auto trip = kill.check_floating(account, rules)) {
      res.kill_switch_tripped = true;
      sm.transition(app::AppState::Flattening);
      close_pos(mark);
      sm.transition(app::AppState::Halted);
      break;
    }
    if (auto trip = kill.check_daily(account, rules, engine.daily_snapshot())) {
      res.kill_switch_tripped = true;
      sm.transition(app::AppState::Flattening);
      close_pos(mark);
      sm.transition(app::AppState::Halted);
      break;
    }

    // 4. Strategy + risk core. Only one position per asset at a time.
    if (pos) continue;
    auto intent = strat->on_market(snap);
    if (!intent) continue;
    if (intent->intent_uuid.empty()) intent->intent_uuid = ulid.next();
    auto decision = engine.evaluate(*intent);
    if (decision.outcome == schemas::v1::RiskOutcomeV1::Reject ||
        !decision.command.has_value()) {
      continue;
    }
    // Exercise the real verify+submit path (HMAC, expiry, state machine).
    om.execute(*decision.command);

    // Open the position in the ledger at the current mark.
    const auto& cmd = *decision.command;
    detail::LedgerPos lp;
    lp.is_long = (cmd.position_side == "long");
    lp.qty = cmd.quantity_nano;
    lp.entry = detail::slip(mark, /*buy=*/lp.is_long, rc.slippage_bps);
    lp.stop = intent->stop_loss_price_micro;
    lp.tp = intent->take_profit_price_micro;
    balance -= detail::fee_on(lp.entry, lp.qty, rc.taker_fee_bps);
    pos = lp;
    ++res.trades;
  }

  // Mark-to-close any position left open at the end of the series.
  if (pos && !ticks.empty()) close_pos(mark_now());
  res.final_equity = balance;  // flat at end -> equity == balance
  res.net_pnl = res.final_equity - res.initial_equity;
  res.survived = !res.kill_switch_tripped;

  loader.unload_all();
  return res;
}

}  // namespace propr::simharness
