#pragma once

#include <string>
#include <string_view>

#include "propr/account/account.h"
#include "propr/account/challenge_rules.h"
#include "propr/app/state_machine.h"
#include "propr/core/clock.h"
#include "propr/core/ulid.h"
#include "propr/risk/kill_switch.h"
#include "propr/risk/leverage_cap.h"
#include "propr/risk/rate_limiter.h"
#include "propr/risk/sizing_policy.h"
#include "propr/schemas/v1.h"

namespace propr::risk {

// The single gatekeeper. Strategies emit IntentV1, this returns RiskDecisionV1.
// When the outcome is Approve or Resize, the decision carries a signed OrderCommandV1
// with an expiry. The OrderManager will refuse anything else.
class RiskEngine {
 public:
  struct Config {
    int max_slippage_bps{20};
    int command_ttl_ms{5000};   // executor rejects commands older than TTL
  };

  RiskEngine(const account::Account& account,
             const account::ChallengeRules& rules,
             const app::StateMachine& sm,
             KillSwitch& kill,
             RateLimiter& rate,
             LeverageCap leverage,
             SizingPolicy sizing,
             core::Ulid& ulid,
             const core::Clock& clock,
             std::string_view hmac_secret,
             Config cfg);

  // Convenience overload for callers that want defaults.
  RiskEngine(const account::Account& account,
             const account::ChallengeRules& rules,
             const app::StateMachine& sm,
             KillSwitch& kill,
             RateLimiter& rate,
             LeverageCap leverage,
             SizingPolicy sizing,
             core::Ulid& ulid,
             const core::Clock& clock,
             std::string_view hmac_secret)
      : RiskEngine(account, rules, sm, kill, rate, leverage, sizing, ulid, clock,
                   hmac_secret, Config{}) {}

  void set_daily_snapshot(core::Money equity) { daily_snapshot_ = equity; }
  core::Money daily_snapshot() const { return daily_snapshot_; }

  schemas::v1::RiskDecisionV1 evaluate(const schemas::v1::IntentV1& intent);

 private:
  schemas::v1::OrderCommandV1 build_signed_command_(
      const schemas::v1::IntentV1& intent, core::Qty final_qty);

  const account::Account& account_;
  const account::ChallengeRules& rules_;
  const app::StateMachine& sm_;
  KillSwitch& kill_;
  RateLimiter& rate_;
  LeverageCap leverage_;
  SizingPolicy sizing_;
  core::Ulid& ulid_;
  const core::Clock& clock_;
  std::string hmac_secret_;
  Config cfg_;
  core::Money daily_snapshot_{0};
};

}  // namespace propr::risk
