#include <gtest/gtest.h>

#include "fakes/fake_clock.h"
#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/app/state_machine.h"
#include "propr/core/ulid.h"
#include "propr/risk/kill_switch.h"
#include "propr/risk/leverage_cap.h"
#include "propr/risk/rate_limiter.h"
#include "propr/risk/risk_engine.h"
#include "propr/risk/sizing_policy.h"
#include "propr/schemas/sign.h"
#include "propr/schemas/v1.h"

using namespace propr;

namespace {
struct Fixture {
  test_support::FakeClock clock{0};
  account::Account account;
  account::ChallengeRules rules;
  app::StateMachine sm;
  risk::RateLimiter rate{10'000, 5000, clock};
  risk::KillSwitch kill{{}, clock};
  risk::LeverageCap lev{3, 2};
  risk::SizingPolicy sizing;
  core::Ulid ulid;
  const std::string secret = "test-secret";
  risk::RiskEngine engine{account, rules, sm, kill, rate, lev,
                          sizing, ulid, clock, secret};

  Fixture() {
    rules.max_overall_dd_abs = core::usdc(500);
    rules.max_daily_loss_abs = core::usdc(200);
    account.apply_account_update(core::usdc(10000), 0, 0, 0, core::usdc(10000));
    engine.set_daily_snapshot(core::usdc(10000));
    sm.transition(app::AppState::Reconciling);
    sm.transition(app::AppState::Live);
  }
};

schemas::v1::IntentV1 reasonable_long(double mark = 60'000.0, double stop = 59'700.0) {
  schemas::v1::IntentV1 i;
  i.intent_uuid = "I";
  i.strategy_name = "t";
  i.kind = schemas::v1::IntentKindV1::OpenLong;
  i.asset_base = "BTC";
  i.suggested_entry_price_micro = static_cast<core::Price>(mark * core::kMicroPerUnit);
  i.stop_loss_price_micro = static_cast<core::Price>(stop * core::kMicroPerUnit);
  i.quantity_nano = 100'000;
  return i;
}
}  // namespace

TEST(RiskEngineTest, ApprovesReasonableEntry) {
  Fixture s;
  auto d = s.engine.evaluate(reasonable_long());
  EXPECT_NE(d.outcome, schemas::v1::RiskOutcomeV1::Reject);
  ASSERT_TRUE(d.command.has_value());
  EXPECT_TRUE(schemas::verify(*d.command, s.secret));
  EXPECT_GT(d.command->expires_at_ns, d.command->issued_at_ns);
  EXPECT_EQ(d.command->intent_uuid, "I");
  EXPECT_EQ(d.command->asset_base, "BTC");
  EXPECT_EQ(d.command->entry_side, "buy");
  EXPECT_EQ(d.command->position_side, "long");
}

TEST(RiskEngineTest, RejectsWhenStateNotLive) {
  Fixture s;
  s.sm.transition(propr::app::AppState::Blind);
  auto d = s.engine.evaluate(reasonable_long());
  EXPECT_EQ(d.outcome, schemas::v1::RiskOutcomeV1::Reject);
  EXPECT_EQ(d.reason, schemas::v1::RiskReasonV1::StateNotLive);
  EXPECT_FALSE(d.command.has_value());
}

TEST(RiskEngineTest, RejectsWhenKillSwitchArmed) {
  Fixture s;
  s.kill.force_arm(propr::core::KillSwitchTrip::Reason::ManualHalt, "x");
  auto d = s.engine.evaluate(reasonable_long());
  EXPECT_EQ(d.outcome, schemas::v1::RiskOutcomeV1::Reject);
  EXPECT_EQ(d.reason, schemas::v1::RiskReasonV1::KillSwitchArmed);
}

TEST(RiskEngineTest, RejectsMissingStop) {
  Fixture s;
  auto i = reasonable_long();
  i.stop_loss_price_micro.reset();
  auto d = s.engine.evaluate(i);
  EXPECT_EQ(d.outcome, schemas::v1::RiskOutcomeV1::Reject);
  EXPECT_EQ(d.reason, schemas::v1::RiskReasonV1::InvalidIntent);
}

TEST(RiskEngineTest, FlattenAllowedInBlind) {
  Fixture s;
  s.sm.transition(propr::app::AppState::Blind);
  schemas::v1::IntentV1 i;
  i.intent_uuid = "F";
  i.kind = schemas::v1::IntentKindV1::Flatten;
  i.asset_base = "BTC";
  auto d = s.engine.evaluate(i);
  EXPECT_EQ(d.outcome, schemas::v1::RiskOutcomeV1::Approve);
}

TEST(RiskEngineTest, SignedCommandTamperingIsDetected) {
  Fixture s;
  auto d = s.engine.evaluate(reasonable_long());
  ASSERT_TRUE(d.command.has_value());
  auto tampered = *d.command;
  tampered.quantity_nano *= 2;
  EXPECT_FALSE(schemas::verify(tampered, s.secret));
}
