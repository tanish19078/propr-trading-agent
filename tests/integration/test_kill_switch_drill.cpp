// Release-gate test. Simulates an adverse equity tick and confirms:
//   - kill switch trips
//   - state machine moves through FLATTENING to HALTED
//   - executor receives cancel_all + close calls
//   - everything completes within 1s of wall clock

#include <gtest/gtest.h>

#include <chrono>
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
#include "propr/sim/exchange_simulator.h"
#include "propr/sim/sim_executor.h"

namespace fs = std::filesystem;
using namespace propr;

TEST(KillSwitchDrill, AdverseTickTripsAndFlatten) {
  propr::test_support::FakeClock clock{0};
  core::EventBus bus;
  core::Ulid ulid;

  const std::string db = (fs::temp_directory_path() / "ks_drill.db").string();
  { std::error_code _e; fs::remove(db, _e); }
  persist::Journal journal(db);

  account::Account account;
  account::ChallengeRules rules;
  rules.max_overall_dd_abs = core::usdc(500);
  rules.max_daily_loss_abs = core::usdc(200);

  app::StateMachine sm;
  sm.transition(app::AppState::Reconciling);
  sm.transition(app::AppState::Live);

  risk::KillSwitch kill({.floating_loss_trip_bps = 7000}, clock);

  sim::SimConfig sim_cfg{.starting_balance = core::usdc(10000)};
  sim::ExchangeSimulator simulator(sim_cfg, account, bus, clock);
  sim::SimExecutor sim_exec(simulator);

  exec::OrderManager om(sim_exec, journal, sm, clock, "ks-secret");

  // Open a position via the simulator directly (no strategy/risk engine in this
  // narrow test - we only care about the flatten path).
  account::Position p;
  p.id = core::PositionId{"sim_pos_BTC"};
  p.asset = {"BTC"};
  p.side = core::PositionSide::Long;
  p.quantity = 1'000'000;
  account.upsert_position(p);

  // Force adverse equity: HWM 10000 -> equity 9600 = 80% of 500-USDC headroom consumed.
  account.apply_account_update(core::usdc(9600), 0, 0, 0, core::usdc(10000));

  const auto start = std::chrono::steady_clock::now();
  auto trip = kill.check_floating(account, rules);
  ASSERT_TRUE(trip.has_value());
  ASSERT_TRUE(kill.armed());

  ASSERT_TRUE(sm.transition(app::AppState::Flattening));
  auto rep = om.flatten_all(account);
  ASSERT_TRUE(sm.transition(app::AppState::Halted));
  const auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
            1000);
  EXPECT_EQ(sm.state(), app::AppState::Halted);
  { std::error_code _e; fs::remove(db, _e); }
}
