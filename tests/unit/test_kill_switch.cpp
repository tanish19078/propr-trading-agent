#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/risk/kill_switch.h"

using propr::account::Account;
using propr::account::ChallengeRules;
using propr::core::usdc;
using propr::risk::KillSwitch;
using propr::test_support::FakeClock;

namespace {
ChallengeRules rules(int dd, int dl) {
  ChallengeRules r;
  r.max_overall_dd_abs = usdc(dd);
  r.max_daily_loss_abs = usdc(dl);
  return r;
}
}  // namespace

TEST(KillSwitchTest, FloatingTripsAt70PercentDdConsumed) {
  FakeClock clock{0};
  KillSwitch::Tunables t;
  t.floating_loss_trip_bps = 7000;
  KillSwitch ks(t, clock);
  Account a;
  // HWM 1000, DD room 100 (10%). Equity 940 = 60 consumed (60%) — no trip.
  a.apply_account_update(usdc(940), 0, 0, 0, usdc(1000));
  EXPECT_FALSE(ks.check_floating(a, rules(100, 50)).has_value());
  EXPECT_FALSE(ks.armed());
  // Equity 920 = 80 consumed (80%) — trip.
  a.apply_account_update(usdc(920), 0, 0, 0, usdc(1000));
  EXPECT_TRUE(ks.check_floating(a, rules(100, 50)).has_value());
  EXPECT_TRUE(ks.armed());
}

TEST(KillSwitchTest, ArmIsIdempotent) {
  FakeClock clock{0};
  KillSwitch::Tunables t;
  KillSwitch ks(t, clock);
  Account a;
  a.apply_account_update(usdc(900), 0, 0, 0, usdc(1000));
  EXPECT_TRUE(ks.check_floating(a, rules(100, 50)).has_value());
  // Second call must NOT re-trip (returns nullopt to avoid double-journal).
  EXPECT_FALSE(ks.check_floating(a, rules(100, 50)).has_value());
}

TEST(KillSwitchTest, DailyTripsRelativeToSnapshot) {
  FakeClock clock{0};
  KillSwitch::Tunables t;
  KillSwitch ks(t, clock);
  Account a;
  a.apply_account_update(usdc(910), 0, 0, 0, usdc(1000));
  // Daily snapshot 1000, max daily loss 100. Equity 910 = 90 consumed (90%) → trip.
  EXPECT_TRUE(ks.check_daily(a, rules(500, 100), usdc(1000)).has_value());
}

TEST(KillSwitchTest, WsDisconnectAfterThreshold) {
  FakeClock clock{1'000'000'000LL};  // 1s after epoch
  KillSwitch::Tunables t;
  t.ws_blind_mode_after_ms = 5000;
  KillSwitch ks(t, clock);
  // Last event was at 0 ns (never connected) — no trip regardless of gap.
  EXPECT_FALSE(ks.check_ws_disconnect(0, "propr").has_value());
  clock.set(6'000'000'000LL);  // 6s after epoch
  EXPECT_FALSE(ks.check_ws_disconnect(0, "propr").has_value());
  // Last event at 1s, now at 6s → gap = 5s → trip.
  EXPECT_TRUE(ks.check_ws_disconnect(1'000'000'000LL, "propr").has_value());
}

TEST(KillSwitchTest, ResetClearsArmed) {
  FakeClock clock{0};
  KillSwitch ks(KillSwitch::Tunables{}, clock);
  ks.force_arm(propr::core::KillSwitchTrip::Reason::ManualHalt, "test");
  EXPECT_TRUE(ks.armed());
  ks.reset();
  EXPECT_FALSE(ks.armed());
}
