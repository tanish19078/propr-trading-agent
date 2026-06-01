// The full risk-core drill, simulator-only. No network. No live API. Proves the
// whole pipeline (Strategy intent -> RiskEngine signed command -> OrderManager
// verify+journal -> SimExecutor -> simulator fills -> Account mirror updates ->
// kill switch trips on adverse equity -> flatten emitted) works deterministically.

#include <gtest/gtest.h>

#include <filesystem>

#include "fakes/fake_clock.h"
#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/app/state_machine.h"
#include "propr/core/event_bus.h"
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

namespace fs = std::filesystem;
using namespace propr;
using propr::core::usdc;

namespace {
schemas::v1::IntentV1 mk_long_intent(double mark, double stop) {
  schemas::v1::IntentV1 i;
  i.intent_uuid = "test_intent";
  i.strategy_name = "drill";
  i.kind = schemas::v1::IntentKindV1::OpenLong;
  i.asset_base = "BTC";
  i.quantity_nano = 100'000;  // 0.0001 BTC
  i.suggested_entry_price_micro =
      static_cast<core::Price>(mark * core::kMicroPerUnit);
  i.stop_loss_price_micro =
      static_cast<core::Price>(stop * core::kMicroPerUnit);
  return i;
}
}  // namespace

TEST(SimulatorDrill, RiskCoreFlattensOnFloatingLossUnderHostileSim) {
  // Wire up the entire risk core, with a simulator that drops snapshots and 429s
  // on 10% of calls.
  propr::test_support::FakeClock clock{1'700'000'000'000'000'000LL};
  core::EventBus bus;
  core::Ulid ulid;

  const std::string db = (fs::temp_directory_path() / "drill.db").string();
  { std::error_code _e; fs::remove(db, _e); }
  persist::Journal journal(db);

  account::Account account;
  account::ChallengeRules rules;
  rules.initial_balance = usdc(10000);
  rules.profit_target_abs = usdc(1000);
  rules.max_overall_dd_abs = usdc(500);
  rules.max_daily_loss_abs = usdc(200);

  app::StateMachine sm;
  ASSERT_TRUE(sm.transition(app::AppState::Reconciling));

  risk::RateLimiter rate(10000, 5000, clock);
  risk::LeverageCap lev(3, 2);
  risk::SizingPolicy sizing;
  risk::KillSwitch kill({.floating_loss_trip_bps = 7000}, clock);

  sim::SimConfig sim_cfg{
      .seed = 0xCAFEBABE,
      .rejection_probability = 0.0,
      .rate_limit_probability = 0.10,
      .partial_fill_probability = 0.05,
      .fill_skip_probability = 0.0,
      .drop_snapshot_probability = 0.10,
      .duplicate_snapshot_probability = 0.05,
      .stale_snapshot_ticks = 0,
      .fill_latency_ticks = 2,
      .starting_balance = usdc(10000),
  };
  sim::ExchangeSimulator simulator(sim_cfg, account, bus, clock);
  sim::SimExecutor sim_exec(simulator);

  const std::string secret = "drill-secret";
  risk::RiskEngine engine(account, rules, sm, kill, rate, lev, sizing, ulid, clock,
                          secret, {.max_slippage_bps = 20, .command_ttl_ms = 5000});
  engine.set_daily_snapshot(account.equity());
  exec::OrderManager om(sim_exec, journal, sm, clock, secret);

  ASSERT_TRUE(sm.transition(app::AppState::Live));

  // Send 50 ticks at 60,000 USDC/BTC; then start a downtrend to 58,500 over 100 more.
  auto send_tick = [&](double price) {
    schemas::v1::TickV1 t;
    t.base = "BTC";
    t.mark_micro = static_cast<core::Price>(price * core::kMicroPerUnit);
    t.at_ns = clock.now_ns();
    simulator.tick(t);
    clock.advance(1'000'000'000LL);  // 1s
  };

  for (int i = 0; i < 50; ++i) send_tick(60'000.0 + (i % 5) * 10.0);

  // Open a long.
  auto intent = mk_long_intent(60'000.0, 59'700.0);
  auto decision = engine.evaluate(intent);
  ASSERT_NE(decision.outcome, schemas::v1::RiskOutcomeV1::Reject)
      << "reason=" << static_cast<int>(decision.reason);
  ASSERT_TRUE(decision.command.has_value());
  auto report = om.execute(*decision.command);
  ASSERT_TRUE(report.status == schemas::v1::ExecutionStatusV1::Accepted ||
              report.status == schemas::v1::ExecutionStatusV1::NetworkError)
      << "first send";

  // Adverse path: drop 3%. Each tick the simulator marks-to-market and publishes.
  // The bus is drained by hand here to exercise the account update flow.
  for (int i = 0; i < 100; ++i) {
    const double price = 60'000.0 - i * 15.0;  // -1500 USDC over 100 ticks = -2.5%
    send_tick(price);
    bus.drain([&](core::Event& ev) {
      std::visit(
          [&](auto&& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, core::AccountUpdated>) {
              account.apply_account_update(e.balance, e.total_unrealized_pnl,
                                           e.isolated_position_margin,
                                           e.cross_position_margin, e.high_water_mark);
            }
          },
          ev);
    });
    if (auto trip = kill.check_floating(account, rules)) {
      ASSERT_TRUE(sm.transition(app::AppState::Flattening));
      om.flatten_all(account);
      ASSERT_TRUE(sm.transition(app::AppState::Halted));
      break;
    }
  }

  EXPECT_TRUE(kill.armed())
      << "kill switch should have tripped on the downtrend; equity="
      << account.equity();
  EXPECT_EQ(sm.state(), app::AppState::Halted);

  { std::error_code _e; fs::remove(db, _e); }
}

TEST(SimulatorDrill, NoEntriesInBlindState) {
  propr::test_support::FakeClock clock{0};
  core::EventBus bus;
  core::Ulid ulid;
  const std::string db = (fs::temp_directory_path() / "drill_blind.db").string();
  { std::error_code _e; fs::remove(db, _e); }
  persist::Journal journal(db);

  account::Account account;
  account::ChallengeRules rules;
  rules.max_overall_dd_abs = usdc(500);
  rules.max_daily_loss_abs = usdc(200);

  app::StateMachine sm;
  sm.transition(app::AppState::Reconciling);
  sm.transition(app::AppState::Live);
  sm.transition(app::AppState::Blind);

  risk::RateLimiter rate(10000, 5000, clock);
  risk::LeverageCap lev(3, 2);
  risk::SizingPolicy sizing;
  risk::KillSwitch kill({}, clock);

  sim::ExchangeSimulator simulator({.starting_balance = usdc(10000)}, account, bus, clock);
  sim::SimExecutor sim_exec(simulator);

  const std::string secret = "k";
  risk::RiskEngine engine(account, rules, sm, kill, rate, lev, sizing, ulid, clock,
                          secret);
  engine.set_daily_snapshot(account.equity());

  auto i = mk_long_intent(60'000.0, 59'700.0);
  auto d = engine.evaluate(i);
  EXPECT_EQ(d.outcome, schemas::v1::RiskOutcomeV1::Reject);
  EXPECT_EQ(d.reason, schemas::v1::RiskReasonV1::StateNotLive);
  EXPECT_FALSE(d.command.has_value());

  { std::error_code _e; fs::remove(db, _e); }
}
